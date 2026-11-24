#include "video.h"
#include "diagnostics.h"
#include <libavutil/log.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#ifdef __SWITCH__
#include <libavutil/hwcontext_nvtegra.h>
#include <pthread.h>
#endif
#include <stdarg.h>
#include <limits.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
struct KVideo {
    const uint8_t *data;size_t size,pos;
    AVFormatContext *format;AVIOContext *io;
    AVCodecContext *video,*audio;AVFrame *frame,*sound,*latest;AVPacket *packet;
    struct SwsContext *scale;SwrContext *resample;
    int vi,ai,eof,pending,finished,range_first;int64_t range_start,range_end;int64_t origin,pts,repeat_until;uint64_t ticks,decoded,written;
    AVFrame *cache[128];int64_t cache_pts[128];
    unsigned cache_count,cache_at;size_t cache_bytes;
    int cache_first,cache_last,cache_enabled,cache_complete,cache_playing,seek_check;
    int hardware_enabled,unaligned_download,keep_surface;
    enum AVPixelFormat hw_format;
    AVFrame *download,*surface;
    KVideoStats stats;
    char error[160];
};
#ifdef __SWITCH__
static KVideoMode default_mode=KVIDEO_AUTO;
static AVBufferRef *nvtegra_driver;
static pthread_mutex_t nvtegra_driver_lock=PTHREAD_MUTEX_INITIALIZER;
#else
static KVideoMode default_mode=KVIDEO_SOFTWARE;
#endif
void kvideo_set_default_mode(KVideoMode mode){default_mode=mode==KVIDEO_SOFTWARE?KVIDEO_SOFTWARE:KVIDEO_AUTO;}
void kvideo_silence_library_logs(void){av_log_set_level(AV_LOG_QUIET);}
#if QLIE_DIAGNOSTICS
static void video_log(const char *format,...){
    fputs("[video] ",stderr);
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
    fputc('\n',stderr);
}
#else
#define video_log(...) ((void)0)
#endif
#ifdef __SWITCH__
static int retain_nvtegra_driver(void){
    if(pthread_mutex_lock(&nvtegra_driver_lock))return 0;
    // The SDK keeps the first AVBufferRef itself as its shared lookup.
    // Own that reference outside individual hardware devices.
    if(!nvtegra_driver){
        nvtegra_driver=av_nvtegra_driver_init();
        if(nvtegra_driver)video_log("NVTEGRA shared driver reference acquired");
    }
    int retained=nvtegra_driver!=NULL;
    pthread_mutex_unlock(&nvtegra_driver_lock);
    return retained;
}
#endif
void kvideo_release_hardware(void){
#ifdef __SWITCH__
    av_buffer_unref(&nvtegra_driver);
#endif
}
void kvideo_enable_surface(KVideo *v){if(v)v->keep_surface=1;}
static void software_fallback(KVideo *v,const char *reason,int rc){
#if QLIE_DIAGNOSTICS
    char detail[80]="";if(rc<0)av_strerror(rc,detail,sizeof(detail));
    snprintf(v->stats.fallback,sizeof(v->stats.fallback),"%s%s%s",reason,rc<0?": ":"",detail);
#else
    (void)reason;(void)rc;
#endif
    v->stats.hardware_pending=0;v->hardware_enabled=0;video_log("software fallback: %s",v->stats.fallback);
}
int kvideo_stats(const KVideo *v,KVideoStats *stats){
    if(!v||!stats)return -1;
    *stats=v->stats;stats->cached_frames=v->cache_count;return 0;
}
static enum AVPixelFormat video_format(AVCodecContext *c,const enum AVPixelFormat *formats){
    KVideo *v=c->opaque;
    if(v->hardware_enabled&&c->hw_device_ctx)
        for(const enum AVPixelFormat *p=formats;*p!=AV_PIX_FMT_NONE;p++)if(*p==v->hw_format)return *p;
    if(v->hardware_enabled)software_fallback(v,"NVTEGRA format/init unavailable",0);
    for(const enum AVPixelFormat *p=formats;*p!=AV_PIX_FMT_NONE;p++){
        const AVPixFmtDescriptor *desc=av_pix_fmt_desc_get(*p);
        if(desc&&!(desc->flags&AV_PIX_FMT_FLAG_HWACCEL))return *p;
    }
    return AV_PIX_FMT_NONE;
}
static int video_hardware(KVideo *v,AVCodecContext *c,const AVCodec *codec){
    enum AVHWDeviceType type=av_hwdevice_find_type_by_name("nvtegra");
    if(type==AV_HWDEVICE_TYPE_NONE){software_fallback(v,"FFmpeg has no NVTEGRA backend",0);return -1;}
    const AVCodecHWConfig *config=NULL;
    for(int i=0;(config=avcodec_get_hw_config(codec,i));i++)
        if(config->device_type==type&&(config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))break;
    if(!config){software_fallback(v,"codec has no NVTEGRA device configuration",0);return -1;}
#ifdef __SWITCH__
    if(!retain_nvtegra_driver()){software_fallback(v,"NVTEGRA shared driver initialization failed",0);return -1;}
#endif
    AVBufferRef *device=NULL;int rc=av_hwdevice_ctx_create(&device,type,NULL,NULL,0);
    if(rc<0){av_buffer_unref(&device);software_fallback(v,"NVTEGRA device creation failed",rc);return -1;}
    v->hw_format=config->pix_fmt;v->hardware_enabled=1;v->stats.hardware_pending=1;
    c->hw_device_ctx=device;c->opaque=v;c->get_format=video_format;
    /* Codec owns its reference; downloaded frames never retain the pool. */
    video_log("NVTEGRA device ready for %s; awaiting first hardware frame",codec->name);return 0;
}
static int fail(KVideo *v,const char *s){snprintf(v->error,sizeof(v->error),"%s",s);return -1;}
static int read_data(void *ctx,uint8_t *out,int n){KVideo *v=ctx;if(n<0)return AVERROR(EINVAL);if(v->pos==v->size)return AVERROR_EOF;size_t count=(size_t)n;if(count>v->size-v->pos)count=v->size-v->pos;memcpy(out,v->data+v->pos,count);v->pos+=count;return (int)count;}
static int64_t seek_data(void *ctx,int64_t n,int whence){
    KVideo *v=ctx;if(whence==AVSEEK_SIZE)return (int64_t)v->size;
    int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?(int64_t)v->pos:whence==SEEK_END?(int64_t)v->size:-1;
    if(base<0||n< -base||n>(int64_t)v->size-base)return AVERROR(EINVAL);
    v->pos=(size_t)(base+n);return (int64_t)v->pos;
}
static AVCodecContext *decoder(AVFormatContext *f,int index,KVideo *owner,KVideoMode mode){
    if(index<0)return NULL;
    const AVCodec *codec=avcodec_find_decoder(f->streams[index]->codecpar->codec_id);
    AVCodecContext *c=codec?avcodec_alloc_context3(codec):NULL;
    if(!c)return NULL;
    c->thread_count=1;
    if(avcodec_parameters_to_context(c,f->streams[index]->codecpar)<0){avcodec_free_context(&c);return NULL;}
    if(owner){
        c->opaque=owner;c->get_format=video_format;
        if(mode==KVIDEO_AUTO)(void)video_hardware(owner,c,codec);
    }
    int rc=avcodec_open2(c,codec,NULL);
    if(rc<0){
        int retry=owner&&owner->stats.hardware_pending;
        avcodec_free_context(&c);
        if(!retry)return NULL;
        software_fallback(owner,"NVTEGRA codec open failed",rc);
        return decoder(f,index,owner,KVIDEO_SOFTWARE);
    }
    return c;
}
int kvideo_is_container(const uint8_t *d,size_t n){
    return d&&n>=4&&(!memcmp(d,"VSD1",4)||!memcmp(d,"OggS",4)||!memcmp(d,"\x1a\x45\xdf\xa3",4));
}
KVideo *kvideo_open(const uint8_t *d,size_t n){return kvideo_open_mode(d,n,default_mode);}
KVideo *kvideo_open_mode(const uint8_t *d,size_t n,KVideoMode mode){
    if(n<8||!kvideo_is_container(d,n))return NULL;
    size_t skip=0;
    if(!memcmp(d,"VSD1",4)){
        uint64_t header=(uint32_t)d[4]|((uint32_t)d[5]<<8)|((uint32_t)d[6]<<16)|((uint32_t)d[7]<<24);
        if(header>n-8)return NULL;
        skip=8+(size_t)header;if(skip>=n)return NULL;
    }
    KVideo *v=calloc(1,sizeof(*v));if(!v)return NULL;
    v->data=d+skip;v->size=n-skip;v->vi=v->ai=-1;v->origin=AV_NOPTS_VALUE;v->hw_format=AV_PIX_FMT_NONE;
    uint8_t *buffer=av_malloc(32768);if(!buffer)goto bad;
    v->io=avio_alloc_context(buffer,32768,0,v,read_data,NULL,seek_data);if(!v->io){av_free(buffer);goto bad;}
    v->format=avformat_alloc_context();if(!v->format)goto bad;v->format->pb=v->io;v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if(avformat_open_input(&v->format,NULL,NULL,NULL)<0||avformat_find_stream_info(v->format,NULL)<0)goto bad;
    v->vi=av_find_best_stream(v->format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    v->ai=av_find_best_stream(v->format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    v->video=decoder(v->format,v->vi,v,mode);if(!v->video||v->video->width<1||v->video->height<1||v->video->width>2048||v->video->height>2048)goto bad;
    if(v->ai>=0){
        v->audio=decoder(v->format,v->ai,NULL,KVIDEO_SOFTWARE);if(!v->audio)goto bad;
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if(swr_alloc_set_opts2(&v->resample,&stereo,AV_SAMPLE_FMT_S16,44100,&v->audio->ch_layout,v->audio->sample_fmt,v->audio->sample_rate,0,NULL)<0||swr_init(v->resample)<0)goto bad;
    }
    v->download=av_frame_alloc();v->frame=av_frame_alloc();v->sound=av_frame_alloc();v->packet=av_packet_alloc();if(!v->download||!v->frame||!v->sound||!v->packet)goto bad;return v;
 bad:kvideo_close(v);return NULL;
}
static void cache_clear(KVideo *v){
    for(unsigned i=0;i<v->cache_count;i++)av_frame_free(&v->cache[i]);
    v->cache_count=v->cache_at=0;v->cache_bytes=0;v->cache_complete=v->cache_playing=0;
}
static int seek_video(KVideo *v,int64_t target){
    if(av_seek_frame(v->format,v->vi,target,AVSEEK_FLAG_BACKWARD)<0)return fail(v,"MOV seek failed");
    avcodec_flush_buffers(v->video);av_frame_unref(v->frame);av_frame_unref(v->download);av_packet_unref(v->packet);
    if(v->surface)av_frame_unref(v->surface);
    v->eof=v->pending=v->finished=0;return 0;
}
KVideo *kvideo_reopen_software(const KVideo *v,uint64_t tick){
    if(!v||v->range_end||tick>=INT64_MAX)return NULL;
    if(v->origin==AV_NOPTS_VALUE&&tick>=60)return NULL;
    KVideo *retry=kvideo_open_mode(v->data,v->size,KVIDEO_SOFTWARE);
    if(!retry||!tick||v->origin==AV_NOPTS_VALUE)return retry;
    int64_t elapsed=av_rescale_q((int64_t)tick,(AVRational){1,60},AV_TIME_BASE_Q);
    if(elapsed>=0&&v->origin<=INT64_MAX-elapsed){
        AVRational base=retry->format->streams[retry->vi]->time_base;
        int64_t target=av_rescale_q_rnd(v->origin+elapsed,AV_TIME_BASE_Q,base,AV_ROUND_DOWN);
        if(seek_video(retry,target)==0){retry->origin=v->origin;return retry;}
    }
    kvideo_close(retry);
    return NULL;
}
int kvideo_rewind(KVideo *v){
    if(!v||v->range_end||v->error[0])return -1;
    const int64_t start=v->format->streams[v->vi]->start_time;
    if(seek_video(v,start==AV_NOPTS_VALUE?0:start)<0)return -1;
    if(v->audio){
        avcodec_flush_buffers(v->audio);
        av_frame_unref(v->sound);
        swr_close(v->resample);
        if(swr_init(v->resample)<0)return fail(v,"video audio rewind failed");
    }
    v->ticks=v->decoded=0;
    v->pts=v->repeat_until=0;
    v->origin=AV_NOPTS_VALUE;
    if(QLIE_DIAGNOSTICS) ++v->stats.rewinds;
    return 0;
}
int kvideo_range(KVideo *v,int first,int last){
    if(!v||first<0||last<=first||v->audio)return -1;
    AVStream *stream=v->format->streams[v->vi];
    int64_t origin=stream->start_time==AV_NOPTS_VALUE?0:av_rescale_q(stream->start_time,stream->time_base,AV_TIME_BASE_Q);
    int64_t start=av_rescale_q(first,(AVRational){1,30},AV_TIME_BASE_Q);
    int replay=v->cache_complete&&v->cache_first==first&&v->cache_last==last;
    if(!replay){
        cache_clear(v);v->cache_enabled=1;v->cache_first=first;v->cache_last=last;
        /* MPEG program-stream seeks can land AFTER the requested keyframe
           when its DTS precedes its PTS. Seek earlier, then decode forward. */
        int64_t preroll=origin+start-1000000;if(preroll<0)preroll=0;
        int64_t target=av_rescale_q(preroll,AV_TIME_BASE_Q,stream->time_base);
        if(seek_video(v,target))return -1;
        v->seek_check=target!=0;
    }else{av_frame_unref(v->frame);v->cache_at=0;v->cache_playing=1;}
    v->eof=v->pending=v->finished=0;v->ticks=0;v->origin=origin;v->range_first=first;v->range_start=start;v->range_end=av_rescale_q(last,(AVRational){1,30},AV_TIME_BASE_Q);
    return 0;
}
void kvideo_close(KVideo *v){if(!v)return;
    if(v->video)video_log("close: hardware=%llu software=%llu cached=%u rewinds=%llu%s%s",(unsigned long long)v->stats.hardware_frames,(unsigned long long)v->stats.software_frames,v->cache_count,(unsigned long long)v->stats.rewinds,v->error[0]?" error=":"",v->error);
    cache_clear(v);av_frame_free(&v->latest);av_frame_free(&v->download);av_frame_free(&v->surface);av_packet_free(&v->packet);av_frame_free(&v->frame);av_frame_free(&v->sound);sws_freeContext(v->scale);swr_free(&v->resample);avcodec_free_context(&v->video);avcodec_free_context(&v->audio);avformat_close_input(&v->format);if(v->io){av_freep(&v->io->buffer);avio_context_free(&v->io);}free(v);}
const char *kvideo_error(KVideo *v){return v->error;}
uint64_t kvideo_decoded_frames(KVideo *v){return v->decoded;}
uint64_t kvideo_written_frames(const KVideo *v){return v->written;}
/* Standalone clips grow geometrically. Video ticks return separately owned
   short PCM buffers to their caller, so their capacity is local to the tick. */
static int reserve_pcm(uint8_t **pcm,size_t needed,size_t *capacity){
    size_t allocation=needed?needed:4;
    if(capacity){
        if(*capacity>=allocation)return 0;
        size_t next=*capacity?*capacity:16384;
        while(next<allocation)next*=2;
        allocation=next;
    }
    uint8_t *p=realloc(*pcm,allocation);
    if(!p)return -1;
    *pcm=p;if(capacity)*capacity=allocation;return 0;
}
static int audio_frames(KVideo *v,uint8_t **pcm,size_t *bytes,size_t *capacity){
    int rc;while((rc=avcodec_receive_frame(v->audio,v->sound))>=0){
        int samples=swr_get_out_samples(v->resample,v->sound->nb_samples);
        if(samples<0||samples>1000000||*bytes>64*1024*1024-(size_t)samples*4)return fail(v,"video PCM limit");
        if(reserve_pcm(pcm,*bytes+(size_t)samples*4,capacity))return fail(v,"video PCM allocation failed");
        uint8_t *out=*pcm+*bytes;int count=swr_convert(v->resample,&out,samples,(const uint8_t **)v->sound->extended_data,v->sound->nb_samples);
        av_frame_unref(v->sound);if(count<0)return fail(v,"audio conversion failed");*bytes+=(size_t)count*4;
    }
    return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:fail(v,"audio decode failed");
}
/* Shared resampler tail for VSD audio and standalone voice streams. */
static int audio_tail(KVideo *v,uint8_t **pcm,size_t *bytes,size_t *capacity){
    int samples=swr_get_out_samples(v->resample,0);
    if(samples<0||samples>1000000||*bytes>64*1024*1024-(size_t)samples*4)return fail(v,"audio tail limit");
    if(samples){
        if(reserve_pcm(pcm,*bytes+(size_t)samples*4,capacity))return fail(v,"video PCM allocation failed");
        uint8_t *out=*pcm+*bytes;int count=swr_convert(v->resample,&out,samples,NULL,0);
        if(count<0)return fail(v,"audio tail conversion failed");
        *bytes+=(size_t)count*4;
    }
    return 0;
}
int kaudio_decode(const uint8_t *data,size_t size,uint8_t **pcm,size_t *bytes){
    if(!pcm||!bytes)return -1;
    *pcm=NULL;*bytes=0;if(!data||!size||size>256*1024*1024)return -1;
    KVideo *v=calloc(1,sizeof(*v));if(!v)return -1;
    size_t capacity=0;int result=-1;v->data=data;v->size=size;v->ai=v->vi=-1;
    uint8_t *buffer=av_malloc(32768);if(!buffer)goto done;
    v->io=avio_alloc_context(buffer,32768,0,v,read_data,NULL,seek_data);if(!v->io){av_free(buffer);goto done;}
    v->format=avformat_alloc_context();if(!v->format)goto done;
    v->format->pb=v->io;v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if(avformat_open_input(&v->format,NULL,NULL,NULL)<0||avformat_find_stream_info(v->format,NULL)<0)goto done;
    v->ai=av_find_best_stream(v->format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    if(v->ai==AVERROR_STREAM_NOT_FOUND){result=KAUDIO_NO_STREAM;goto done;}
    v->audio=decoder(v->format,v->ai,NULL,KVIDEO_SOFTWARE);
    if(!v->audio||v->audio->sample_rate<8000||v->audio->sample_rate>192000||v->audio->ch_layout.nb_channels<1||v->audio->ch_layout.nb_channels>2)goto done;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    if(swr_alloc_set_opts2(&v->resample,&stereo,AV_SAMPLE_FMT_S16,44100,&v->audio->ch_layout,v->audio->sample_fmt,v->audio->sample_rate,0,NULL)<0||swr_init(v->resample)<0)goto done;
    v->sound=av_frame_alloc();v->packet=av_packet_alloc();if(!v->sound||!v->packet)goto done;
    for(unsigned guard=0;guard<100000;guard++){
        int rc=av_read_frame(v->format,v->packet);
        if(rc==AVERROR_EOF){
            if(avcodec_send_packet(v->audio,NULL)<0||audio_frames(v,pcm,bytes,&capacity)||audio_tail(v,pcm,bytes,&capacity)||!*bytes)goto done;
            result=0;break;
        }
        if(rc<0)goto done;
        if(v->packet->stream_index==v->ai){rc=avcodec_send_packet(v->audio,v->packet);if(rc>=0)rc=audio_frames(v,pcm,bytes,&capacity);}
        av_packet_unref(v->packet);if(rc<0)goto done;
    }
 done:kvideo_close(v);
    if(result){free(*pcm);*pcm=NULL;*bytes=0;}
    else if(capacity>*bytes){uint8_t *trimmed=realloc(*pcm,*bytes);if(trimmed)*pcm=trimmed;}
    return result;
}
static void free_download_buffer(void *opaque,uint8_t *data){
    (void)opaque;
    free(data);
}
/* NVTEGRA maps the backing page range into VIC and needs 256-byte plane
   addresses and pitches. The AVBuffer owns the whole page-aligned range. */
static int prepare_nv12_download(AVFrame *dst,const AVFrame *src,const AVHWFramesContext *context){
    const size_t page=4096;
    const int pitch_alignment=256;
    int size=av_image_get_buffer_size(AV_PIX_FMT_NV12,context->width,context->height,pitch_alignment);
    if(size<0)return size;
    size_t bytes=((size_t)size+AV_INPUT_BUFFER_PADDING_SIZE+page-1)&~(page-1);
    uint8_t *pixels=aligned_alloc(page,bytes);
    if(!pixels)return AVERROR(ENOMEM);
    AVBufferRef *buffer=av_buffer_create(pixels,bytes,free_download_buffer,NULL,0);
    if(!buffer){free(pixels);return AVERROR(ENOMEM);}
    int rc=av_image_fill_arrays(dst->data,dst->linesize,pixels,AV_PIX_FMT_NV12,
                                context->width,context->height,pitch_alignment);
    if(rc<0){av_buffer_unref(&buffer);return rc;}
    memset(pixels+size,0,bytes-(size_t)size);
    dst->buf[0]=buffer;dst->extended_data=dst->data;
    dst->format=AV_PIX_FMT_NV12;dst->width=src->width;dst->height=src->height;
    return 0;
}
static int describe_surface(const AVFrame *frame,KVideoSurface *out){
#ifdef __SWITCH__
    if(!frame||frame->format!=AV_PIX_FMT_NVTEGRA||!frame->hw_frames_ctx||!frame->buf[0]||
       frame->width!=1280||frame->height!=720||frame->crop_left||frame->crop_right||
       frame->crop_top||frame->crop_bottom)return 0;
    const AVHWFramesContext *ctx=(const AVHWFramesContext *)frame->hw_frames_ctx->data;
    if(ctx->sw_format!=AV_PIX_FMT_NV12||frame->linesize[0]<frame->width||frame->linesize[1]<frame->width)return 0;
    AVNVTegraMap *map=av_nvtegra_frame_get_fbuf_map(frame);
    void *storage=av_nvtegra_map_get_addr(map);
    uint32_t size=av_nvtegra_map_get_size(map);
    uintptr_t y=(uintptr_t)storage,uv=(uintptr_t)frame->data[1];
    if(!storage||frame->data[0]!=storage||uv<=y||uv-y>=size)return 0;
    if(out)*out=(KVideoSurface){.frame=frame,.storage=storage,.storage_size=size,.handle=av_nvtegra_map_get_handle(map),
                                .chroma_offset=(uint32_t)(uv-y),.pitch={(uint32_t)frame->linesize[0],(uint32_t)frame->linesize[1]},
                                .width=(uint32_t)frame->width,.height=(uint32_t)frame->height,.linear=map->is_linear};
    return 1;
#else
    (void)frame;(void)out;return 0;
#endif
}
int kvideo_get_surface(const KVideo *v,KVideoSurface *out){return v&&describe_surface(v->surface,out);}

static int download_frame(KVideo *v,const AVFrame *frame){
    av_frame_unref(v->download);
    int rc=AVERROR(ENOSYS);
    const AVHWFramesContext *context=frame->hw_frames_ctx?(const AVHWFramesContext *)frame->hw_frames_ctx->data:NULL;
    if(!v->unaligned_download&&context&&context->sw_format==AV_PIX_FMT_NV12){
        rc=prepare_nv12_download(v->download,frame,context);
        if(rc>=0)rc=av_hwframe_transfer_data(v->download,frame,0);
        if(rc<0){
#if QLIE_DIAGNOSTICS
            char detail[80];av_strerror(rc,detail,sizeof(detail));
            video_log("NVTEGRA aligned NV12 download unavailable: %s",detail);
#endif
            v->unaligned_download=1;av_frame_unref(v->download);
        }else if(!v->stats.hardware_frames)video_log("NVTEGRA aligned NV12 download active");
    }
    if(rc<0)rc=av_hwframe_transfer_data(v->download,frame,0);
    if(rc<0){
#if QLIE_DIAGNOSTICS
        char detail[80];av_strerror(rc,detail,sizeof(detail));video_log("NVTEGRA download failed: %s",detail);
#endif
        return fail(v,"NVTEGRA frame download failed");
    }
    const AVPixFmtDescriptor *downloaded=av_pix_fmt_desc_get(v->download->format);
    if(!downloaded||(downloaded->flags&AV_PIX_FMT_FLAG_HWACCEL))return fail(v,"video download did not produce a CPU frame");
    return 0;
}

/* Range playback keeps CPU frames in its loop cache. Unrestricted playback
   may keep the decoded surface for the renderer. */
static int video_download(KVideo *v){
    const AVPixFmtDescriptor *desc=av_pix_fmt_desc_get(v->frame->format);
    if(!desc)return fail(v,"video pixel format invalid");
    if(desc->flags&AV_PIX_FMT_FLAG_HWACCEL){
        if(v->frame->format!=v->hw_format)return fail(v,"unexpected video hardware format");
        if(v->keep_surface&&!v->range_end&&describe_surface(v->frame,NULL)){
            if(!v->stats.hardware_frames)video_log("NVTEGRA active: first hardware NV12 surface (%dx%d)",v->frame->width,v->frame->height);
            v->stats.hardware_frames++;v->stats.hardware_pending=0;return 0;
        }
        if(download_frame(v,v->frame))return -1;
        if(av_frame_copy_props(v->download,v->frame)<0)return fail(v,"video frame properties allocation failed");
        av_frame_unref(v->frame);av_frame_move_ref(v->frame,v->download);
        /* CPU output must not retain a reference to the decoder frame pool. */
        av_buffer_unref(&v->frame->hw_frames_ctx);
        if(!v->stats.hardware_frames)video_log("NVTEGRA active: first hardware frame downloaded (%dx%d)",v->frame->width,v->frame->height);
        v->stats.hardware_frames++;v->stats.hardware_pending=0;
    }else{
        if(v->stats.hardware_pending)software_fallback(v,"decoder produced software frames",0);
        if(QLIE_DIAGNOSTICS) v->stats.software_frames++;
    }
    return 0;
}
static int next(KVideo *v,uint8_t **pcm,size_t *bytes){
    for(unsigned guard=0;guard<100000;guard++){
        int rc=avcodec_receive_frame(v->video,v->frame);
        if(rc>=0){
            if(video_download(v))return -1;
            int64_t pts=v->frame->best_effort_timestamp;
            if(pts!=AV_NOPTS_VALUE){pts=av_rescale_q(pts,v->format->streams[v->vi]->time_base,AV_TIME_BASE_Q);if(v->origin==AV_NOPTS_VALUE)v->origin=pts;v->pts=pts-v->origin;}
            else v->pts=(int64_t)v->decoded*1000000/60;
            v->decoded++;v->pending=1;return 0;
        }
        if(rc==AVERROR_EOF){v->finished=1;return 1;}
        if(rc!=AVERROR(EAGAIN))return fail(v,"video decode failed");
        if(v->eof)return fail(v,"video drain stalled");
        rc=av_read_frame(v->format,v->packet);
        if(rc<0){
            if(rc!=AVERROR_EOF)return fail(v,"video demux failed");
            v->eof=1;if(avcodec_send_packet(v->video,NULL)<0)return fail(v,"video flush failed");
            if(v->audio&&(avcodec_send_packet(v->audio,NULL)<0||audio_frames(v,pcm,bytes,NULL)))return fail(v,"audio flush failed");
            if(v->audio&&audio_tail(v,pcm,bytes,NULL))return -1;
            continue;
        }
        if(v->packet->stream_index==v->vi){
            /* A zero-length Theora packet repeats the previous picture. The
               send_packet API rejects it; retain the displayed picture through
               its timestamp instead of submitting an empty/flush packet. */
            if(v->video->codec_id==AV_CODEC_ID_THEORA&&v->packet->size==0){
                if(v->packet->pts!=AV_NOPTS_VALUE&&v->origin!=AV_NOPTS_VALUE){
                    int64_t end=av_rescale_q(v->packet->pts+(v->packet->duration>0?v->packet->duration:1),v->format->streams[v->vi]->time_base,AV_TIME_BASE_Q)-v->origin;
                    if(end>v->repeat_until)v->repeat_until=end;
                }
                av_packet_unref(v->packet);continue;
            }
            rc=avcodec_send_packet(v->video,v->packet);
        }
        else if(v->packet->stream_index==v->ai){rc=avcodec_send_packet(v->audio,v->packet);if(rc>=0)rc=audio_frames(v,pcm,bytes,NULL);}else rc=0;
        if(rc<0){
            char detail[128];av_strerror(rc,detail,sizeof(detail));
            video_log("packet error: stream=%d size=%d pts=%lld code=%d (%s)",v->packet->stream_index,v->packet->size,(long long)v->packet->pts,rc,detail);
            av_packet_unref(v->packet);return fail(v,detail);
        }
        av_packet_unref(v->packet);
    }
    return fail(v,"video packet budget exceeded");
}
static int write_pixels(KVideo *v,const AVFrame *frame,KImage *screen){
    v->scale=sws_getCachedContext(v->scale,frame->width,frame->height,frame->format,(int)screen->width,(int)screen->height,AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!v->scale)return fail(v,"video scaler failed");
    uint8_t *dst[]={screen->pixels,NULL,NULL,NULL};int stride[]={(int)screen->stride,0,0,0};
    if(sws_scale(v->scale,(const uint8_t *const*)frame->data,frame->linesize,0,frame->height,dst,stride)<0)return fail(v,"video conversion failed");
    return 0;
}
static int present(KVideo *v,AVFrame *frame,KImage *screen){
    if(v->surface)av_frame_unref(v->surface);
    if(frame->format==v->hw_format&&v->keep_surface){
        if(!v->surface)v->surface=av_frame_alloc();
        if(!v->surface||av_frame_ref(v->surface,frame)<0)return fail(v,"video surface reference failed");
    }else if(write_pixels(v,frame,screen))return -1;
    ++v->written;return 0;
}
int kvideo_copy_surface(KVideo *v,KImage *screen){
    if(!kvideo_get_surface(v,NULL)||download_frame(v,v->surface))return -1;
    int result=write_pixels(v,v->download,screen);
    av_frame_unref(v->download);
    return result;
}
AVFrame *kvideo_clone_frame(const AVFrame *frame){return av_frame_clone(frame);}
void kvideo_free_frame(AVFrame *frame){av_frame_free(&frame);}
int kvideo_read_frame(const AVFrame *frame,KImage *screen,char *error,size_t error_size){
    KVideo reader={0};
    reader.stats.hardware_frames=1; // This decoded frame was already reported by its player.
    reader.download=av_frame_alloc();
    if(!reader.download){
        snprintf(error,error_size,"Video frame allocation failed");
        return -1;
    }
    int result=download_frame(&reader,frame);
    if(!result)result=write_pixels(&reader,reader.download,screen);
    if(result)snprintf(error,error_size,"%s",reader.error);
    av_frame_free(&reader.download);
    sws_freeContext(reader.scale);
    return result;
}
static void cache_frame(KVideo *v){
    if(!v->range_end||!v->cache_enabled)return;
    size_t bytes=0;
    for(unsigned i=0;i<AV_NUM_DATA_POINTERS;i++)if(v->frame->buf[i])bytes+=v->frame->buf[i]->size;
    for(int i=0;i<v->frame->nb_extended_buf;i++)bytes+=v->frame->extended_buf[i]->size;
    if(v->cache_count==128||bytes>64*1024*1024-v->cache_bytes){cache_clear(v);v->cache_enabled=0;return;}
    AVFrame *frame=av_frame_clone(v->frame);
    if(!frame){cache_clear(v);v->cache_enabled=0;return;}
    v->cache[v->cache_count]=frame;v->cache_pts[v->cache_count++]=v->pts;v->cache_bytes+=bytes;
}
static int present_latest(KVideo *v,AVFrame *latest,KImage *screen){
    if(!latest||latest->format==AV_PIX_FMT_NONE)return 0;
    return present(v,latest,screen);
}
static int step_video(KVideo *v,KImage *screen,uint8_t **pcm,size_t *bytes,int64_t clock,AVFrame *latest){
    if(!v)return -1;
    if(!screen||!screen->pixels||!screen->width||!screen->height||screen->stride>INT_MAX||
       screen->stride<(size_t)screen->width*4||!pcm||!bytes)return fail(v,"video target surface invalid");
    if(v->error[0])return -1;
    if(v->finished)return clock>=v->repeat_until?1:0;
    if(v->range_end&&clock>=v->range_end){
        v->finished=1;if(v->cache_enabled&&v->cache_count)v->cache_complete=1;return 1;
    }
    if(v->cache_playing){
        while(v->cache_at<v->cache_count&&v->cache_pts[v->cache_at]<=clock+1){
            if(present(v,v->cache[v->cache_at++],screen))return -1;
        }
        return 0;
    }
    for(unsigned guard=0;latest||guard<1000;guard++){
        if(!v->pending){int rc=next(v,pcm,bytes);if(rc){
            if(rc<0||present_latest(v,latest,screen))return -1;
            return rc==1&&clock<v->repeat_until?0:rc;
        }}
        if(v->seek_check){
            v->seek_check=0;
            if(v->pts>v->range_start+1){if(seek_video(v,0))return -1;continue;}
        }
        if(v->range_end&&v->pts+1<v->range_start){av_frame_unref(v->frame);v->pending=0;continue;}
        if(v->range_end&&v->pts+1>=v->range_end)return 0;
        if(v->pts>clock+1)return present_latest(v,latest,screen);
        cache_frame(v);
        if(latest){av_frame_unref(latest);av_frame_move_ref(latest,v->frame);}
        else if(present(v,v->frame,screen))return -1;
        av_frame_unref(v->frame);v->pending=0;
    }
    return fail(v,"video frame budget exceeded");
}

int kvideo_step(KVideo *v,KImage *screen,uint8_t **pcm,size_t *bytes){
    if(!v)return -1;
    /* Match timestamp rounding rather than introducing a 2/3/1 cadence. */
    const int64_t clock=av_rescale_q((int64_t)v->range_first*2+(int64_t)v->ticks++,(AVRational){1,60},AV_TIME_BASE_Q);
    return step_video(v,screen,pcm,bytes,clock,NULL);
}
int kvideo_advance(KVideo *v,uint64_t tick,KImage *screen,uint8_t **pcm,size_t *bytes){
    if(!v||tick<v->ticks||tick>=INT64_MAX||v->range_end)return -1;
    if(!v->latest){v->latest=av_frame_alloc();if(!v->latest)return fail(v,"video frame allocation failed");}
    const int64_t clock=av_rescale_q((int64_t)tick,(AVRational){1,60},AV_TIME_BASE_Q);
    v->ticks=tick+1;
    const int result=step_video(v,screen,pcm,bytes,clock,v->latest);
    av_frame_unref(v->latest);
    return result;
}

int kvideo_has_audio(const KVideo *v){return v&&v->audio!=NULL;}
void kvideo_disable_audio(KVideo *v){
    v->ai=-1;avcodec_free_context(&v->audio);swr_free(&v->resample);
}

unsigned kvideo_source_width(const KVideo *v){return v&&v->video?(unsigned)v->video->width:0;}
unsigned kvideo_source_height(const KVideo *v){return v&&v->video?(unsigned)v->video->height:0;}
double kvideo_duration_ms(const KVideo *v){
    if(!v)return 0;
    const AVStream *stream=v->format->streams[v->vi];
    if(stream->duration>0)return stream->duration*av_q2d(stream->time_base)*1000;
    return v->format->duration>0?v->format->duration*(1000.0/AV_TIME_BASE):0;
}
