/* Copyright (c) 2026 Plan-B Foundation
 * Distributed under the MIT software license, see the accompanying COPYING file.
 */
#include "adapters/protocol/qr_image.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PngBudget PngBudget;
typedef union PngBlock PngBlock;
union PngBlock {
    max_align_t alignment;
    struct {
        PngBudget* owner;
        PngBlock* previous;
        PngBlock* next;
        size_t bytes;
    } fields;
};
struct PngBudget {
    struct roots_gui_qr_png_stats stats;
    size_t active;
    PngBlock* blocks;
};
static _Thread_local PngBudget* decoding;
static _Atomic size_t active_png_bytes;

size_t roots_gui_qr_png_active_bytes(void)
{
    return atomic_load_explicit(&active_png_bytes,memory_order_relaxed);
}

static int png_reserve(size_t charge)
{
    size_t total=roots_gui_qr_png_active_bytes();
    do {
        if (charge>ROOTS_GUI_QR_PNG_MAX_ACTIVE-total) return 0;
    } while (!atomic_compare_exchange_weak_explicit(&active_png_bytes,&total,total+charge,
                  memory_order_relaxed,memory_order_relaxed));
    return 1;
}

static void* png_allocate(size_t bytes)
{
    PngBudget* owner=decoding;
    if (!owner) return NULL;
    if (bytes>SIZE_MAX-sizeof(PngBlock)) {
        ++owner->stats.refused_allocations;
        return NULL;
    }
    const size_t charge=bytes+sizeof(PngBlock);
    if (charge>owner->stats.decode_limit-owner->active || !png_reserve(charge)) {
        ++owner->stats.refused_allocations;
        return NULL;
    }
    PngBlock* allocated=malloc(charge);
    if (!allocated) {
        atomic_fetch_sub_explicit(&active_png_bytes,charge,memory_order_relaxed);
        ++owner->stats.refused_allocations;
        return NULL;
    }
    allocated->fields.owner=owner;
    allocated->fields.previous=NULL;
    allocated->fields.next=owner->blocks;
    allocated->fields.bytes=bytes;
    if (owner->blocks) owner->blocks->fields.previous=allocated;
    owner->blocks=allocated;
    owner->active+=charge;
    if (owner->active>owner->stats.peak_bytes) owner->stats.peak_bytes=owner->active;
    return allocated+1;
}

/* Detach this header from its thread's decode before returning an image.
 * Its process-wide charge survives until the consumer releases the buffer. */
static void png_detach(PngBlock* block)
{
    PngBudget* owner=block->fields.owner;
    if (!owner) return;
    if (block->fields.previous) block->fields.previous->fields.next=block->fields.next;
    else owner->blocks=block->fields.next;
    if (block->fields.next) block->fields.next->fields.previous=block->fields.previous;
    owner->active-=block->fields.bytes+sizeof(PngBlock);
    block->fields.owner=NULL;
}

void roots_gui_qr_png_free(void* memory)
{
    if (!memory) return;
    PngBlock* block=(PngBlock*)memory-1;
    const size_t charge=block->fields.bytes+sizeof(PngBlock);
    png_detach(block);
    free(block);
    atomic_fetch_sub_explicit(&active_png_bytes,charge,memory_order_relaxed);
}

static void* png_resize(void* memory,size_t bytes)
{
    if (!memory) return png_allocate(bytes);
    if (!bytes) {
        roots_gui_qr_png_free(memory);
        return NULL;
    }
    PngBlock* before=(PngBlock*)memory-1;
    const size_t old_size=before->fields.bytes;
    if (bytes<=old_size) return memory; /* The reserved block size is unchanged. */
    /* Charge both buffers before copying. A failed resize keeps the old
     * pointer and its charge exactly as realloc would. */
    void* after=png_allocate(bytes);
    if (!after) return NULL;
    memcpy(after,memory,old_size);
    roots_gui_qr_png_free(memory);
    return after;
}

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_FAILURE_STRINGS
#define STBI_MAX_DIMENSIONS ROOTS_GUI_QR_PNG_MAX_SIDE
#define STBI_MALLOC(bytes) png_allocate(bytes)
#define STBI_REALLOC(memory,bytes) png_resize(memory,bytes)
#define STBI_FREE(memory) roots_gui_qr_png_free(memory)
#include "third_party/qr/stb/stb_image.h"

struct PngShape {
    size_t width;
    size_t height;
    size_t scanlines;
    size_t compressed;
};
static uint32_t png_u32(const unsigned char* p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | (uint32_t)p[3];
}
static size_t png_rows(size_t width,size_t height,size_t bits,int interlaced)
{
    if (!interlaced) return height*((width*bits+7)/8+1);
    /* Adam7 pass starts and strides. Each nonempty row has one filter byte. */
    static const unsigned x[]={0,4,0,2,0,1,0},y[]={0,0,4,0,2,0,1};
    static const unsigned dx[]={8,8,4,4,2,2,1},dy[]={8,8,8,4,4,2,2};
    size_t bytes=0;
    for (size_t pass=0;pass<7;++pass) {
        if (width<=x[pass] || height<=y[pass]) continue;
        const size_t cols=(width-x[pass]+dx[pass]-1)/dx[pass];
        const size_t rows=(height-y[pass]+dy[pass]-1)/dy[pass];
        bytes+=rows*((cols*bits+7)/8+1);
    }
    return bytes;
}

static int png_shape(const unsigned char* source,size_t bytes,struct PngShape* shape)
{
    static const unsigned char signature[]={137,80,78,71,13,10,26,10};
    static const unsigned channels[]={1,0,3,1,2,0,4};
    if (bytes<33 || bytes>ROOTS_GUI_QR_PNG_MAX_FILE || memcmp(source,signature,8)
        || png_u32(source+8)!=13 || memcmp(source+12,"IHDR",4)) return 0;
    shape->width=png_u32(source+16);
    shape->height=png_u32(source+20);
    if (shape->width<21 || shape->height<21 || shape->width>ROOTS_GUI_QR_PNG_MAX_SIDE
        || shape->height>ROOTS_GUI_QR_PNG_MAX_SIDE) return 0;
    const unsigned depth=source[24],color=source[25];
    if (color>=sizeof(channels)/sizeof(*channels) || !channels[color]
        || source[26]!=0 || source[27]!=0 || source[28]>1) return 0;
    const int packed=(depth==1 || depth==2 || depth==4) && (color==0 || color==3);
    if (!packed && depth!=8 && (depth!=16 || color==3)) return 0;
    shape->scanlines=png_rows(shape->width,shape->height,depth*channels[color],source[28]);
    int finished_idat=0;
    for (size_t offset=33;bytes-offset>=12;) {
        const size_t length=png_u32(source+offset);
        const unsigned char* type=source+offset+4;
        if (length>bytes-offset-12) return 0;
        offset+=length+12;
        if (!memcmp(type,"IDAT",4)) {
            if (finished_idat) return 0;
            shape->compressed+=length;
        } else if (!memcmp(type,"IEND",4)) {
            return length==0 && offset==bytes && shape->compressed>=6;
        } else if (!memcmp(type,"IHDR",4)) {
            return 0;
        } else if (shape->compressed) {
            finished_idat=1;
        }
    }
    return 0;
}

static int png_check_inflate(const unsigned char* source,const struct PngShape* shape)
{
    unsigned char* compressed=png_allocate(shape->compressed);
    unsigned char* scanlines=png_allocate(shape->scanlines);
    int valid=0;
    if (compressed && scanlines) {
        size_t copied=0;
        /* The framing and cumulative length were checked on these same bytes. */
        for (size_t offset=33;copied<shape->compressed;) {
            const size_t length=png_u32(source+offset);
            if (!memcmp(source+offset+4,"IDAT",4)) {
                memcpy(compressed+copied,source+offset+8,length);
                copied+=length;
            }
            offset+=length+12;
        }
        const int written=stbi_zlib_decode_buffer((char*)scanlines,(int)shape->scanlines,
            (const char*)compressed,(int)shape->compressed);
        if (written==(int)shape->scanlines) {
            unsigned a=1,b=0;
            for (size_t i=0;i<shape->scanlines;++i) {
                a=(a+scanlines[i])%65521;
                b=(b+a)%65521;
            }
            valid=(b<<16|a)==png_u32(compressed+shape->compressed-4);
        }
    }
    roots_gui_qr_png_free(scanlines);
    roots_gui_qr_png_free(compressed);
    return valid;
}

unsigned char* roots_gui_qr_png_decode(const unsigned char* source,size_t bytes,int* width,int* height,
    struct roots_gui_qr_png_stats* stats)
{
    if (stats) memset(stats,0,sizeof(*stats));
    struct PngShape shape={0};
    if (decoding || !source || !width || !height || !png_shape(source,bytes,&shape)) return NULL;
    const size_t pixels=shape.width*shape.height;
    /* The admitted dimensions and input length make these sums fit size_t.
     * Charge compressed input copies, filtered scanlines, palettes, converted
     * samples, and every simultaneous allocation made by the pinned decoder. */
    PngBudget budget={0};
    const size_t working_limit=64*1024+24*pixels+3*bytes;
    budget.stats.decode_limit=working_limit<ROOTS_GUI_QR_PNG_MAX_DECODE ? working_limit : ROOTS_GUI_QR_PNG_MAX_DECODE;
    budget.stats.expected_scanline_bytes=shape.scanlines;
    decoding=&budget;
    unsigned char* result=NULL;
    if (png_check_inflate(source,&shape)) {
        int x=0,y=0,components=0;
        unsigned char* rgba=stbi_load_from_memory(source,(int)bytes,&x,&y,&components,2);
        if (rgba && x==(int)shape.width && y==(int)shape.height) {
            result=png_allocate(pixels);
            if (result) {
                for (size_t i=0;i<pixels;++i) {
                    const unsigned alpha=rgba[2*i+1];
                    result[i]=(unsigned char)((rgba[2*i]*alpha+255*(255-alpha)+127)/255);
                }
                *width=x;
                *height=y;
                PngBlock* retained=(PngBlock*)result-1;
                budget.stats.retained_luma_bytes=retained->fields.bytes+sizeof(PngBlock);
                png_detach(retained);
            }
        }
        roots_gui_qr_png_free(rgba);
    } else {
        budget.stats.invalid_inflate=1;
    }
    while (budget.blocks) roots_gui_qr_png_free(budget.blocks+1);
    decoding=NULL;
    budget.stats.decoder_bytes_at_return=budget.active;
    if (stats) *stats=budget.stats;
    return result;
}
