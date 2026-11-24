#ifndef KISAKU_FONT_H
#define KISAKU_FONT_H
#include "rmt.h"
typedef struct KFont KFont;
typedef struct {
    size_t glyphs,bitmap_bytes,glyph_limit,bitmap_limit;
    uint64_t hits,rasterizations;
} KFontCacheStats;
/* A user font path, or platform font (macOS / Switch shared font). */
KFont *kfont_open(const char *path,int simplified);
KFont *kfont_open_face(const char *path,unsigned face_index,int simplified);
/* Returns the number of faces, or -1 on error; label is family and style. */
int kfont_face_info(const char *path,unsigned face_index,char *label,size_t capacity);
void kfont_close(KFont *font);
/* QLIE sampling factor: monochrome rasterization followed by coverage averaging. */
int kfont_set_antialias(KFont *font,unsigned factor);
int kfont_set_style(KFont *font,unsigned style,uint32_t color);
/* Read-only diagnostics for the per-font, colour-independent LRU cache.
   Bitmap bytes exclude fixed hash buckets and bounded entry metadata. */
int kfont_cache_stats(const KFont *font,KFontCacheStats *stats);
typedef struct {int left,top;unsigned width,height;} KFontGlyphBounds;
int kfont_glyph_bounds(KFont*,uint32_t,unsigned,unsigned,KFontGlyphBounds*);
int kfont_draw(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,unsigned width,unsigned height,uint32_t rgb);
/* Draw the configured shadow/outline layers, or the legacy thin edge when
   no style was configured. UI panels can use plain kfont_draw instead. */
int kfont_draw_outline(KFont *font,KImage *dst,uint32_t codepoint,int x,int y,
                       unsigned width,unsigned height,uint32_t rgb);
#endif
