#ifndef KISAKU_VIDEO_H
#define KISAKU_VIDEO_H
#include "rmt.h"
typedef struct KVideo KVideo;
struct AVFrame;
typedef struct KVideoSurface {
    const struct AVFrame *frame;
    void *storage;
    uint64_t version;
    uint32_t storage_size,handle,chroma_offset,pitch[2],width,height;
    int linear;
} KVideoSurface;
typedef enum { KVIDEO_AUTO, KVIDEO_SOFTWARE } KVideoMode;
typedef struct {uint64_t hardware_frames,software_frames,rewinds;unsigned cached_frames;int hardware_pending;char fallback[160];} KVideoStats;
/* Configure once before starting playback/workers. AUTO tries NVTEGRA on
   Switch; the host default stays software. Logs distinguish actual frames
   from successful device creation. */
void kvideo_set_default_mode(KVideoMode mode);
void kvideo_silence_library_logs(void);
/* Call after all players are closed and playback workers have joined. */
void kvideo_release_hardware(void);
/* GPU players can borrow a decoded NV12 surface until the next
   video advance. Clone the AVFrame to use it after that point. */
void kvideo_enable_surface(KVideo *v);
int kvideo_get_surface(const KVideo *v,KVideoSurface *surface);
int kvideo_copy_surface(KVideo *v,KImage *screen);
struct AVFrame *kvideo_clone_frame(const struct AVFrame *frame);
void kvideo_free_frame(struct AVFrame *frame);
int kvideo_read_frame(const struct AVFrame *frame,KImage *screen,char *error,size_t error_size);
KVideo *kvideo_open_mode(const uint8_t *data,size_t size,KVideoMode mode);
int kvideo_stats(const KVideo *v,KVideoStats *stats);
/* Reopen the same bytes in software and seek back to a keyframe for tick.
   The new decoder uses the previous clip's presentation time origin. */
KVideo *kvideo_reopen_software(const KVideo *v,uint64_t tick);
/* Decode audio to owned 44100Hz stereo S16LE PCM, or report a missing stream. */
enum { KAUDIO_NO_STREAM = 1 };
int kaudio_decode(const uint8_t *data,size_t size,uint8_t **pcm,size_t *pcm_size);
/* Borrow VSD, OggTheora or Matroska bytes until close. Output PCM is caller-owned. */
int kvideo_is_container(const uint8_t *data,size_t size);
KVideo *kvideo_open(const uint8_t *data,size_t size);
/* MOV descriptors use the native 30 fps fallback time base; no embedded audio. */
int kvideo_range(KVideo *v,int first,int last);
/* Restart an unrestricted clip while retaining its demuxer and decoder. */
int kvideo_rewind(KVideo *v);
void kvideo_close(KVideo *v);
/* One 60 Hz tick. 0 playing, 1 EOF, -1 decode error. */
int kvideo_step(KVideo *v,KImage *screen,uint8_t **pcm,size_t *pcm_size);
/* Advance an unrestricted clip to an absolute 60 Hz tick, converting only its latest due picture. */
int kvideo_advance(KVideo *v,uint64_t tick,KImage *screen,uint8_t **pcm,size_t *pcm_size);
uint64_t kvideo_decoded_frames(KVideo *v);
uint64_t kvideo_written_frames(const KVideo *v);
int kvideo_has_audio(const KVideo *v);
void kvideo_disable_audio(KVideo *v);
unsigned kvideo_source_width(const KVideo *v);
unsigned kvideo_source_height(const KVideo *v);
double kvideo_duration_ms(const KVideo *v);
const char *kvideo_error(KVideo *v);
#endif
