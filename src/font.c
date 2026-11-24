#include "font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#ifdef __SWITCH__
#include <switch.h>
#endif
enum { FONT_CACHE_BUCKETS=1024, FONT_CACHE_GLYPHS=1024, FONT_CACHE_BYTES=2*1024*1024 };
typedef struct KFontGlyph {
    uint32_t codepoint;
    unsigned width,height;
    FT_Bitmap bitmap;
    int64_t left,top;
    size_t bytes;
    struct KFontGlyph *hash_next,*newer,*older;
} KFontGlyph;
struct KFont {
    FT_Library library;FT_Face faces[3];unsigned count,shared,supersample,style,style_set;
    uint32_t style_color;
    KFontGlyph *glyphs[FONT_CACHE_BUCKETS],*newest,*oldest;
    size_t cache_count,cache_bytes;
    uint64_t cache_hits,rasterizations;
};
static unsigned font_glyph_bucket(uint32_t cp,unsigned width,unsigned height){
    uint32_t key=cp*0x9e3779b1u^(width*0x85ebca6bu)^(height*0xc2b2ae35u);
    return (key^(key>>16))&(FONT_CACHE_BUCKETS-1u);
}
static void font_cache_touch(KFont *f,KFontGlyph *g){
    if(f->newest==g)return;
    if(g->newer)g->newer->older=g->older;
    if(g->older)g->older->newer=g->newer;
    else f->oldest=g->newer;
    g->newer=NULL;g->older=f->newest;
    if(f->newest)f->newest->newer=g;
    f->newest=g;
    if(!f->oldest)f->oldest=g;
}
static void font_cache_evict(KFont *f){
    KFontGlyph *g=f->oldest;if(!g)return;
    unsigned bucket=font_glyph_bucket(g->codepoint,g->width,g->height);
    KFontGlyph **link=&f->glyphs[bucket];
    while(*link&&*link!=g)link=&(*link)->hash_next;
    if(*link)*link=g->hash_next;
    f->oldest=g->newer;
    if(f->oldest)f->oldest->older=NULL;else f->newest=NULL;
    f->cache_bytes-=g->bytes;f->cache_count--;
    free(g->bitmap.buffer);free(g);
}
/* Nintendo's shared fonts are returned as raw SFNT/TTC memory.  FreeType
 * normally selects Unicode automatically, but some Horizon revisions expose
 * a platform-default charmap first; that leaves FT_Get_Char_Index returning
 * zero for every CP932 code point even though the glyphs are present.  Keep
 * the face usable for both the HOS font service and host TTC fallbacks by
 * explicitly selecting Unicode whenever it is available. */
static void select_unicode(FT_Face face){
    if(!face)return;
    /* Do not trust FreeType's first Unicode alias on a raw HOS SFNT/TTC:
       firmware revisions may expose both a BMP and a UCS-4 Microsoft cmap,
       and the alias is not stable.  Prefer the concrete Microsoft maps,
       then Apple's Unicode map, then the first advertised map. */
    FT_CharMap candidate=NULL;
    for(int pass=0;pass<3&&!candidate;pass++)for(int i=0;i<face->num_charmaps;i++){
        FT_CharMap map=face->charmaps[i];
        if(pass==0&&map->platform_id==3&&(map->encoding_id==10||map->encoding_id==1))candidate=map;
        else if(pass==1&&map->platform_id==0)candidate=map;
        else if(pass==2&&!candidate)candidate=map;
    }
    if(candidate)(void)FT_Set_Charmap(face,candidate);
    else (void)FT_Select_Charmap(face,FT_ENCODING_UNICODE);
}
/* A shared font can contain a valid Unicode cmap with only part of the
   repertoire.  Probe the remaining subtables before declaring a glyph
   missing; this is needed for HOS Japanese/Chinese fallback faces and keeps
   history/name panels visible when the preferred cmap is sparse. */
static FT_UInt glyph_index(FT_Face face,uint32_t codepoint){
    FT_UInt index=FT_Get_Char_Index(face,codepoint);if(index)return index;
    FT_CharMap saved=face->charmap;
    for(int i=0;i<face->num_charmaps;i++){
        FT_CharMap map=face->charmaps[i];if(map==saved)continue;
        if(FT_Set_Charmap(face,map))continue;
        index=FT_Get_Char_Index(face,codepoint);if(index)return index;
    }
    if(saved)(void)FT_Set_Charmap(face,saved);
    return 0;
}
void kfont_close(KFont *f){
    if(!f)return;
    while(f->oldest)font_cache_evict(f);
    for(unsigned i=0;i<f->count;i++)FT_Done_Face(f->faces[i]);
    if(f->library)FT_Done_FreeType(f->library);
#ifdef __SWITCH__
    if(f->shared)plExit();
#endif
    free(f);
}
int kfont_set_antialias(KFont *f,unsigned factor){
    if(!f||factor<1||factor>16)return -1;
    if(f->supersample==factor)return 0;
    while(f->oldest)font_cache_evict(f);
    f->supersample=factor;return 1;
}
int kfont_set_style(KFont *f,unsigned style,uint32_t color){
    if(!f||style>15||color>0xffffff)return -1;
    if(f->style_set&&f->style==style&&f->style_color==color)return 0;
    while(f->oldest)font_cache_evict(f);
    f->style=style;f->style_color=color;f->style_set=1;return 1;
}
int kfont_cache_stats(const KFont *f,KFontCacheStats *stats){
    if(!f||!stats)return -1;
    *stats=(KFontCacheStats){f->cache_count,f->cache_bytes,FONT_CACHE_GLYPHS,FONT_CACHE_BYTES,
                           f->cache_hits,f->rasterizations};return 0;
}
int kfont_face_info(const char *path,unsigned face_index,char *label,size_t capacity){
    FT_Library library;FT_Face face;
    if(!path||!label||!capacity||FT_Init_FreeType(&library))return -1;
    if(FT_New_Face(library,path,(FT_Long)face_index,&face)){FT_Done_FreeType(library);return -1;}
    int count=face->num_faces>INT_MAX?-1:(int)face->num_faces;
    snprintf(label,capacity,"%s %s",face->family_name?face->family_name:"",face->style_name?face->style_name:"");
    FT_Done_Face(face);FT_Done_FreeType(library);return count;
}
KFont *kfont_open(const char *path,int simplified){return kfont_open_face(path,0,simplified);}
KFont *kfont_open_face(const char *path,unsigned face_index,int simplified){
    KFont *f=calloc(1,sizeof(*f));if(!f)return NULL;
    if(FT_Init_FreeType(&f->library)){kfont_close(f);return NULL;}
    if(path&&*path){
        if(FT_New_Face(f->library,path,(FT_Long)face_index,&f->faces[0])){kfont_close(f);return NULL;}
        select_unicode(f->faces[0]);
        f->count=1;
    }
    {
#ifdef __SWITCH__
        if(R_FAILED(plInitialize(PlServiceType_User))){if(!f->count){kfont_close(f);return NULL;}}
        else {f->shared=1;
        PlSharedFontType types[]={simplified?PlSharedFontType_ChineseSimplified:PlSharedFontType_Standard,simplified?PlSharedFontType_Standard:PlSharedFontType_ChineseSimplified,PlSharedFontType_ExtChineseSimplified};
        for(unsigned i=0;i<3&&f->count<3;i++){
            PlFontData data;
            if(R_SUCCEEDED(plGetSharedFontByType(&data,types[i]))&&!FT_New_Memory_Face(f->library,data.address,data.size,0,&f->faces[f->count])){
                select_unicode(f->faces[f->count]);
                f->count++;
            }
        }
        }
#elif defined(__APPLE__)
        (void)simplified;
        if(!FT_New_Face(f->library,"/System/Library/Fonts/Hiragino Sans GB.ttc",0,&f->faces[f->count])){select_unicode(f->faces[f->count]);f->count++;}
#else
        (void)simplified;
#endif
    }
    if(!f->count){kfont_close(f);return NULL;}return f;
}
static int font_load_glyph(KFont *f,uint32_t cp,unsigned width,unsigned height,
                           FT_Bitmap **bitmap_out,
                           int64_t *left_out,int64_t *top_out){
    if(!f||width<1||height<1||width>256||height>256)return -1;
    unsigned bucket=font_glyph_bucket(cp,width,height);
    for(KFontGlyph *cached=f->glyphs[bucket];cached;cached=cached->hash_next){
        if(cached->codepoint!=cp||cached->width!=width||cached->height!=height)continue;
        font_cache_touch(f,cached);f->cache_hits++;
        *bitmap_out=&cached->bitmap;*left_out=cached->left;*top_out=cached->top;return 0;
    }
    FT_Face face=NULL;FT_UInt index=0;
    for(unsigned i=0;i<f->count;i++)if((index=glyph_index(f->faces[i],cp))){face=f->faces[i];break;}
    unsigned factor=f->supersample?f->supersample:1;
    FT_Int32 flags=f->supersample?(FT_LOAD_TARGET_MONO|FT_LOAD_MONOCHROME):FT_LOAD_TARGET_NORMAL;
    if(!face||FT_Set_Pixel_Sizes(face,width*factor,height*factor)||FT_Load_Glyph(face,index,flags))return -1;
    if(f->style_set&&(f->style&1)&&!(face->style_flags&FT_STYLE_FLAG_BOLD))FT_GlyphSlot_Embolden(face->glyph);
    if(FT_Render_Glyph(face->glyph,f->supersample?FT_RENDER_MODE_MONO:FT_RENDER_MODE_NORMAL))return -1;
    f->rasterizations++;
    FT_GlyphSlot g=face->glyph;FT_Bitmap *bitmap=&g->bitmap;
    if(bitmap->pixel_mode!=FT_PIXEL_MODE_GRAY&&bitmap->pixel_mode!=FT_PIXEL_MODE_MONO)return -1;
    if(f->supersample){
        int64_t high_left=g->bitmap_left,high_top=(face->size->metrics.ascender>>6)-g->bitmap_top;
        int64_t left=high_left/(int)factor,top=high_top/(int)factor;
        if(high_left<0&&high_left%(int)factor)--left;
        if(high_top<0&&high_top%(int)factor)--top;
        unsigned pad_x=(unsigned)(high_left-left*factor),pad_y=(unsigned)(high_top-top*factor);
        unsigned w=(pad_x+bitmap->width+factor-1)/factor,h=(pad_y+bitmap->rows+factor-1)/factor;
        size_t bytes=(size_t)w*h;
        if(bytes>FONT_CACHE_BYTES)return -1;
        KFontGlyph *cached=calloc(1,sizeof(*cached));if(!cached)return -1;
        uint8_t *pixels=bytes?calloc(bytes,1):NULL;
        if(bytes&&!pixels){free(cached);return -1;}
        for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
            unsigned sum=0;
            for(unsigned yy=0;yy<factor;yy++)for(unsigned xx=0;xx<factor;xx++){
                int sy=(int)(y*factor+yy)-(int)pad_y,sx=(int)(x*factor+xx)-(int)pad_x;
                if(sy<0||sx<0||sy>=(int)bitmap->rows||sx>=(int)bitmap->width)continue;
                const uint8_t *row=bitmap->buffer+(bitmap->pitch>=0?(unsigned)sy:bitmap->rows-(unsigned)sy-1)*(size_t)abs(bitmap->pitch);
                sum+=bitmap->pixel_mode==FT_PIXEL_MODE_MONO?((row[sx/8]&(0x80>>(sx%8)))?255:0):row[sx];
            }
            pixels[(size_t)y*w+x]=(uint8_t)(sum/(factor*factor));
        }
        while(f->cache_count>=FONT_CACHE_GLYPHS||f->cache_bytes>FONT_CACHE_BYTES-bytes)font_cache_evict(f);
        cached->codepoint=cp;cached->width=width;cached->height=height;cached->left=left;cached->top=top;cached->bytes=bytes;
        cached->bitmap=(FT_Bitmap){.rows=h,.width=w,.pitch=(int)w,.buffer=pixels,.num_grays=256,.pixel_mode=FT_PIXEL_MODE_GRAY};
        cached->hash_next=f->glyphs[bucket];f->glyphs[bucket]=cached;
        cached->older=f->newest;if(f->newest)f->newest->newer=cached;else f->oldest=cached;
        f->newest=cached;f->cache_count++;f->cache_bytes+=bytes;
        *bitmap_out=&cached->bitmap;*left_out=left;*top_out=top;return 0;
    }
    *bitmap_out=bitmap;*left_out=g->bitmap_left;*top_out=(face->size->metrics.ascender>>6)-g->bitmap_top;
    /* Store coverage and metrics only: position, colour and the outline are
       applied by the unchanged draw loops. Copying the bitmap also keeps it
       valid when FreeType loads another size into the same glyph slot. */
    size_t pitch=(size_t)abs(bitmap->pitch);
    if(pitch&&bitmap->rows>FONT_CACHE_BYTES/pitch)return 0;
    size_t bytes=pitch*bitmap->rows;
    KFontGlyph *cached=calloc(1,sizeof(*cached));if(!cached)return 0;
    uint8_t *pixels=bytes?malloc(bytes):NULL;
    if(bytes&&!pixels){free(cached);return 0;}
    for(unsigned row=0;row<bitmap->rows&&pitch;row++){
        const uint8_t *src=bitmap->buffer+(bitmap->pitch>=0?row:bitmap->rows-row-1)*pitch;
        memcpy(pixels+row*pitch,src,pitch);
    }
    while(f->cache_count>=FONT_CACHE_GLYPHS||f->cache_bytes>FONT_CACHE_BYTES-bytes)font_cache_evict(f);
    cached->codepoint=cp;cached->width=width;cached->height=height;
    cached->bitmap=*bitmap;cached->bitmap.buffer=pixels;cached->bitmap.pitch=(int)pitch;
    cached->left=*left_out;cached->top=*top_out;cached->bytes=bytes;
    cached->hash_next=f->glyphs[bucket];f->glyphs[bucket]=cached;
    cached->older=f->newest;if(f->newest)f->newest->newer=cached;else f->oldest=cached;
    f->newest=cached;f->cache_count++;f->cache_bytes+=bytes;
    *bitmap_out=&cached->bitmap;return 0;
}
static void font_blend_pixel(KImage *dst,int64_t xx,int64_t yy,unsigned alpha,unsigned rgb){
    if(!alpha||xx<0||yy<0||xx>=dst->width||yy>=dst->height)return;
    uint8_t *p=dst->pixels+(size_t)yy*dst->stride+(size_t)xx*4;
    /* Straight alpha source-over, preserving transparent text layers. */
    unsigned out_alpha=alpha+p[3]*(255-alpha)/255;
    for(unsigned c=0;c<3;c++)p[c]=(uint8_t)((((rgb>>(8*c))&255)*alpha+p[c]*p[3]*(255-alpha)/255)/out_alpha);
    p[3]=(uint8_t)out_alpha;
}
static unsigned font_bitmap_alpha(const FT_Bitmap *bitmap,const uint8_t *source,unsigned col){
    return bitmap->pixel_mode==FT_PIXEL_MODE_MONO?((source[col/8]&(0x80>>(col%8)))?255:0):source[col];
}
int kfont_glyph_bounds(KFont *f,uint32_t cp,unsigned width,unsigned height,KFontGlyphBounds *bounds){
    if(!f||!bounds||width<1||height<1||width>256||height>256)return -1;
    FT_Bitmap *bitmap=NULL;int64_t left=0,top=0;
    if(font_load_glyph(f,cp,width,height,&bitmap,&left,&top))return -1;
    *bounds=(KFontGlyphBounds){(int)left,(int)top,bitmap->width,bitmap->rows};return 0;
}
int kfont_draw(KFont *f,KImage *dst,uint32_t cp,int x,int y,unsigned width,unsigned height,unsigned rgb){
    if(!f||!dst||!dst->pixels||dst->stride<(size_t)dst->width*4||width<1||height<1||width>256||height>256)return -1;
    FT_Bitmap *bitmap=NULL;int64_t left=0,top=0;
    if(font_load_glyph(f,cp,width,height,&bitmap,&left,&top))return -1;
    left+=x;top+=y;
    for(unsigned row=0;row<bitmap->rows;row++){
        int64_t yy=top+row;if(yy<0||yy>=dst->height)continue;
        const uint8_t *source=bitmap->buffer+(bitmap->pitch>=0?row:bitmap->rows-row-1)*(size_t)abs(bitmap->pitch);
        for(unsigned col=0;col<bitmap->width;col++){
            int64_t xx=left+col;if(xx<0||xx>=dst->width)continue;
            font_blend_pixel(dst,xx,yy,font_bitmap_alpha(bitmap,source,col),rgb);
        }
    }
    return 0;
}
int kfont_draw_outline(KFont *f,KImage *dst,uint32_t cp,int x,int y,unsigned width,unsigned height,uint32_t rgb){
    if(!f||!dst||!dst->pixels||dst->stride<(size_t)dst->width*4)return -1;
    FT_Bitmap *bitmap=NULL;int64_t left=0,top=0;
    if(font_load_glyph(f,cp,width,height,&bitmap,&left,&top))return -1;
    left+=x;top+=y;
    if(f->style_set){
        int offsets[9][2];unsigned opacity[9],count=0;
        if(f->style&2){offsets[count][0]=2;offsets[count][1]=2;opacity[count++]=0x66;}
        if(f->style&(4|8)){
            int radius=(f->style&8)?2:1;
            const int direction[8][2]={{-1,0},{1,0},{0,1},{0,-1},{-1,-1},{1,-1},{-1,1},{1,1}};
            for(unsigned i=0;i<((f->style&8)?8u:4u);i++){offsets[count][0]=direction[i][0]*(i<4?radius:1);offsets[count][1]=direction[i][1]*(i<4?radius:1);opacity[count++]=255;}
        }
        for(unsigned pass=0;pass<count;pass++)for(unsigned row=0;row<bitmap->rows;row++){
            const uint8_t *source=bitmap->buffer+(bitmap->pitch>=0?row:bitmap->rows-row-1)*(size_t)abs(bitmap->pitch);
            for(unsigned col=0;col<bitmap->width;col++)font_blend_pixel(dst,left+col+offsets[pass][0],top+row+offsets[pass][1],font_bitmap_alpha(bitmap,source,col)*opacity[pass]/255,f->style_color);
        }
        return kfont_draw(f,dst,cp,x,y,width,height,rgb);
    }
    /* One glyph rasterization, a half-alpha one-pixel core, and a faint
       second outward pixel: the edge reads on bright artwork without making
       the glyph heavy.  This avoids the old nine full FreeType renders per
       glyph that made Switch story text stutter. */
    for(unsigned row=0;row<bitmap->rows;row++){
        const uint8_t *source=bitmap->buffer+(bitmap->pitch>=0?row:bitmap->rows-row-1)*(size_t)abs(bitmap->pitch);
        for(unsigned col=0;col<bitmap->width;col++){
            unsigned alpha=font_bitmap_alpha(bitmap,source,col);if(!alpha)continue;
            int64_t xx=left+(int64_t)col,yy=top+(int64_t)row;
            unsigned edge=alpha/2u;
            font_blend_pixel(dst,xx-1,yy,edge,0x000000);font_blend_pixel(dst,xx+1,yy,edge,0x000000);
            font_blend_pixel(dst,xx,yy-1,edge,0x000000);font_blend_pixel(dst,xx,yy+1,edge,0x000000);
            unsigned outer=alpha/4u;
            font_blend_pixel(dst,xx-2,yy,outer,0x000000);font_blend_pixel(dst,xx+2,yy,outer,0x000000);
            font_blend_pixel(dst,xx,yy-2,outer,0x000000);font_blend_pixel(dst,xx,yy+2,outer,0x000000);
        }
    }
    for(unsigned row=0;row<bitmap->rows;row++){
        const uint8_t *source=bitmap->buffer+(bitmap->pitch>=0?row:bitmap->rows-row-1)*(size_t)abs(bitmap->pitch);
        for(unsigned col=0;col<bitmap->width;col++)font_blend_pixel(dst,left+(int64_t)col,top+(int64_t)row,font_bitmap_alpha(bitmap,source,col),rgb);
    }
    return 0;
}
