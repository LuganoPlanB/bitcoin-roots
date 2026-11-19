/* Copyright (c) 2026 Plan-B Foundation
 * Distributed under the MIT software license, see the accompanying COPYING file.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ROOTS_GUI_QR_PNG_MAX_SIDE = 1536,
    ROOTS_GUI_QR_PNG_MAX_FILE = 4 * 1024 * 1024,
    ROOTS_GUI_QR_PNG_MAX_DECODE = 64 * 1024 * 1024,
    ROOTS_GUI_QR_PNG_MAX_ACTIVE = 96 * 1024 * 1024,
};

struct roots_gui_qr_png_stats {
    size_t decode_limit;
    size_t peak_bytes;
    size_t expected_scanline_bytes;
    size_t decoder_bytes_at_return;
    size_t retained_luma_bytes;
    unsigned refused_allocations;
    int invalid_inflate;
};

/* Returns bounded, alpha-composited luma owned by the caller. Release it
 * with roots_gui_qr_png_free, including after a cancelled QR read. */
unsigned char* roots_gui_qr_png_decode(const unsigned char* source,size_t bytes,int* width,int* height,
    struct roots_gui_qr_png_stats*);
void roots_gui_qr_png_free(void*);
size_t roots_gui_qr_png_active_bytes(void);

#ifdef __cplusplus
}
#endif
