#ifndef VGA_FONT_H
#define VGA_FONT_H

#include <stdint.h>

#define VGA_FONT_WIDTH 8
#define VGA_FONT_HEIGHT 16
#define VGA_FONT_GLYPHS 256

typedef struct {
    uint8_t glyphs[VGA_FONT_GLYPHS][VGA_FONT_HEIGHT][VGA_FONT_WIDTH / 8];
} VgaFont;

extern const VgaFont vga_font_8x16;

#endif // VGA_FONT_H
