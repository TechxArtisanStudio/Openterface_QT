#include "bios_focus_detector.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define LUMINANCE_WHITE_THRESHOLD 210.0f
#define LUMINANCE_DARK_THRESHOLD 80.0f
#define BG_EDGE_MARGIN_RATIO 0.05f     // Sample 5% from each edge for bg detection
#define BG_TOLERANCE 50                // Row bg must be within this of screen bg
#define BG_MIN_LUM 10                  // Reject near-black screens
#define BG_MAX_LUM 245                 // Reject near-white screens
#define TITLE_BAR_MAX_ROW_RATIO 0.08f
#define FOOTER_MIN_ROW_RATIO 0.92f
#define MIN_GROUP_HEIGHT 8
#define MIN_BRIGHT_PIXELS 20
#define CHAR_CELL_WIDTH 26
#define CHAR_CELL_HEIGHT 22
#define OCR_THRESHOLD 200
#define OCR_UPSCALE 3
#define TESSERACT_PSM 7

static float color_luminance(unsigned char r, unsigned char g, unsigned char b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static char* ocr_row(
    const unsigned char* pixels,
    int width,
    int height,
    int y,
    int x_start,
    int x_end
) {
    // Crop the row region matching the parameters that worked in testing:
    // crop: (50, 595, 1000, 625) with x_start=67, x_end=930, mid_y=609
    // We want: x_start-padded to x_end+padded, mid_y-14 to mid_y+16
    int pad_x = 15;
    int crop_x = (x_start - pad_x > 0) ? x_start - pad_x : 0;
    int crop_y = y - 14;
    int crop_w = x_end - crop_x + pad_x;
    int crop_h = 30;

    if (crop_x + crop_w > width) crop_w = width - crop_x;
    if (crop_y < 0) crop_y = 0;
    if (crop_y + crop_h > height) crop_h = height - crop_y;
    if (crop_w <= 0 || crop_h <= 0) return NULL;

    // Create cropped image with preprocessing
    int new_w = crop_w * OCR_UPSCALE;
    int new_h = crop_h * OCR_UPSCALE;
    unsigned char* cropped = malloc(new_w * new_h * 3);
    if (!cropped) return NULL;

    // Crop and upscale with nearest neighbor
    for (int dy = 0; dy < new_h; dy++) {
        for (int dx = 0; dx < new_w; dx++) {
            int sx = crop_x + dx / OCR_UPSCALE;
            int sy = crop_y + dy / OCR_UPSCALE;
            int src_idx = (sy * width + sx) * 3;
            int dst_idx = (dy * new_w + dx) * 3;

            unsigned char r = pixels[src_idx];
            unsigned char g = pixels[src_idx + 1];
            unsigned char b = pixels[src_idx + 2];

            // Threshold: keep only WHITE pixels (lum > OCR_THRESHOLD) as black text,
            // turn everything else (gray bg, blue text, help text) to white background.
            // This gives tesseract a clean black-on-white image containing only the
            // focused (white) text, eliminating help-text bleed from adjacent regions.
            float lum = color_luminance(r, g, b);
            if (lum > OCR_THRESHOLD) {
                // White focused text -> black for tesseract
                cropped[dst_idx] = 0;
                cropped[dst_idx + 1] = 0;
                cropped[dst_idx + 2] = 0;
            } else {
                // Gray background / blue text / help text -> white background
                cropped[dst_idx] = 255;
                cropped[dst_idx + 1] = 255;
                cropped[dst_idx + 2] = 255;
            }
        }
    }

    // Save as PNG
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/bios_ocr_%d_%d", (int)getpid(), (int)(y * 1000 + x_start));
    char input_path[260];
    snprintf(input_path, sizeof(input_path), "%s.png", tmp_path);
    char output_path[260];
    snprintf(output_path, sizeof(output_path), "%s.txt", tmp_path);
    stbi_write_png(input_path, new_w, new_h, 3, cropped, new_w * 3);
    free(cropped);

    // Run tesseract
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tesseract %s %s --psm %d 2>/dev/null", input_path, tmp_path, TESSERACT_PSM);

    int ret = system(cmd);
    if (ret != 0) {
        // Cleanup
        unlink(input_path);
        unlink(output_path);
        return NULL;
    }

    // Read output
    FILE* f = fopen(output_path, "r");
    if (!f) {
        unlink(input_path);
        unlink(output_path);
        return NULL;
    }

    char* text = malloc(BIOS_MAX_TEXT_LEN);
    if (!text) {
        fclose(f);
        unlink(input_path);
        unlink(output_path);
        return NULL;
    }

    fgets(text, BIOS_MAX_TEXT_LEN, f);
    fclose(f);

    // Cleanup temp files
    unlink(input_path);
    unlink(output_path);

    // Trim trailing newline
    size_t len = strlen(text);
    while (len > 0 && (text[len-1] == '\n' || text[len-1] == ' ')) {
        text[--len] = '\0';
    }

    return text;
}

static int detect_highlight_rows(
    const unsigned char* pixels,
    int width,
    int height,
    BiosFocusResult* result
) {
    float* lum = malloc(width * height * sizeof(float));
    if (!lum) return -1;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            lum[y * width + x] = color_luminance(pixels[idx], pixels[idx + 1], pixels[idx + 2]);
        }
    }

    int title_bar_max_row = (int)(height * TITLE_BAR_MAX_ROW_RATIO);
    int footer_min_row = (int)(height * FOOTER_MIN_ROW_RATIO);
    int edge_margin = (int)(width * BG_EDGE_MARGIN_RATIO);
    if (edge_margin < 1) edge_margin = 1;

    /*
     * Step 1: Compute screen background luminance via edge sampling.
     * Sample the leftmost and rightmost 5% of pixels in the content area.
     * Menu items typically start after the edge margin, so edge pixels
     * are almost always background. This works for any BIOS color scheme:
     *   - Aptio: gray bg (lum ~164)
     *   - Phoenix/Award: dark blue bg (lum ~40)
     *   - Others: whatever the background color is
     */
    long long screen_bg_sum = 0;
    int screen_bg_count = 0;
    for (int y = title_bar_max_row; y < footer_min_row; y++) {
        for (int x = 0; x < edge_margin; x++) {
            screen_bg_sum += (int)lum[y * width + x];
            screen_bg_count++;
        }
        for (int x = width - edge_margin; x < width; x++) {
            screen_bg_sum += (int)lum[y * width + x];
            screen_bg_count++;
        }
    }
    int screen_bg_lum = (screen_bg_count > 0) ? (int)(screen_bg_sum / screen_bg_count) : -1;

    /* Reject screens with near-black or near-white backgrounds (likely not a BIOS menu) */
    if (screen_bg_lum < BG_MIN_LUM || screen_bg_lum > BG_MAX_LUM) {
        result->grid_width = width;
        result->grid_height = height;
        result->num_highlights = 0;
        free(lum);
        return 0;
    }

    int* row_bright_count = calloc(height, sizeof(int));
    int* row_bg_lum = calloc(height, sizeof(int));
    if (!row_bright_count || !row_bg_lum) {
        free(lum);
        if (row_bright_count) free(row_bright_count);
        if (row_bg_lum) free(row_bg_lum);
        return -1;
    }

    /*
     * Step 2: For each row, compute:
     *   - row_bright_count: number of pixels with lum > WHITE_THRESHOLD (potential focused text)
     *   - row_bg_lum: background luminance via edge sampling (same approach as screen-level)
     */
    for (int y = 0; y < height; y++) {
        int bright_count = 0;
        long long edge_sum = 0;
        int edge_count = 0;

        for (int x = 0; x < width; x++) {
            float l = lum[y * width + x];
            if (l > LUMINANCE_WHITE_THRESHOLD) {
                bright_count++;
            }
        }

        /* Edge sampling for background luminance */
        for (int x = 0; x < edge_margin; x++) {
            edge_sum += (int)lum[y * width + x];
            edge_count++;
        }
        for (int x = width - edge_margin; x < width; x++) {
            edge_sum += (int)lum[y * width + x];
            edge_count++;
        }

        row_bright_count[y] = bright_count;
        row_bg_lum[y] = (edge_count > 0) ? (int)(edge_sum / edge_count) : -1;
    }

    /*
     * Step 3: Find contiguous groups of "bright" rows (lots of lum > 210 pixels).
     * These are potential highlighted/focused menu items.
     */
    int highlight_count = 0;
    int in_group = 0;
    int group_start = 0;

    for (int y = 0; y <= height && highlight_count < BIOS_MAX_HIGHLIGHTS; y++) {
        int is_content_row = (y >= title_bar_max_row && y < footer_min_row);
        int has_bright = (y < height && row_bright_count[y] > MIN_BRIGHT_PIXELS);

        if (is_content_row && has_bright) {
            if (!in_group) {
                group_start = y;
                in_group = 1;
            }
        } else if (in_group) {
            int group_end = y - 1;
            if (group_end - group_start + 1 >= MIN_GROUP_HEIGHT) {
                int mid_y = (group_start + group_end) / 2;
                int bg_lum = row_bg_lum[mid_y];

                /*
                 * Sanity check: the row's background luminance should be close
                 * to the screen's background luminance. This filters out false
                 * positives where white text appears on a differently-colored
                 * background (e.g., a logo, a popup dialog, etc.)
                 *
                 * This replaces the old hardcoded BG_GRAY_LOW/HIGH check and
                 * works for any BIOS color scheme.
                 */
                int bg_diff = abs(bg_lum - screen_bg_lum);
                if (bg_diff <= BG_TOLERANCE && bg_lum >= BG_MIN_LUM) {
                    int first_x = width, last_x = 0;
                    for (int x = 0; x < width; x++) {
                        if (lum[mid_y * width + x] > LUMINANCE_WHITE_THRESHOLD) {
                            if (x < first_x) first_x = x;
                            if (x > last_x) last_x = x;
                        }
                    }

                    BiosHighlight* hl = &result->highlights[highlight_count++];
                    hl->row = mid_y;
                    hl->col_start = first_x;
                    hl->col_end = last_x;
                    hl->confidence = 0.9f;

                    char* ocr_text = ocr_row(pixels, width, height, mid_y, first_x, last_x);
                    if (ocr_text && strlen(ocr_text) > 0) {
                        snprintf(hl->text, BIOS_MAX_TEXT_LEN, "%s", ocr_text);
                        free(ocr_text);
                    } else {
                        snprintf(hl->text, BIOS_MAX_TEXT_LEN,
                                 "Highlight row y=%d (bg lum %d)", mid_y, bg_lum);
                        if (ocr_text) free(ocr_text);
                    }
                }
            }
            in_group = 0;
        }
    }

    result->grid_width = width;
    result->grid_height = height;
    result->num_highlights = highlight_count;

    free(lum);
    free(row_bright_count);
    free(row_bg_lum);
    return 0;
}

/*
 * Colored-bar highlights.
 *
 * Many text UIs mark the focused item with a solid bar in a colour of its own
 * rather than with brighter text: newt/whiptail and the Debian installer (a
 * red bar), dialog/ncurses, GRUB (an inverted bar). The bright-text detector
 * above cannot see those. Here, a highlight is a rectangle of one colour:
 *   - one text line tall (BAR_MIN_HEIGHT..BAR_MAX_HEIGHT) and clearly wider
 *     than tall;
 *   - bounded: the rows just above and below it are NOT mostly that colour
 *     (this rejects panels and backgrounds, which merely contain text);
 *   - with text inside: some, but not most, of its pixels are another colour
 *     (this rejects shadows and empty blocks).
 * Rows are scanned for long runs of one colour that tolerate short gaps (the
 * glyphs drawn on the bar), and runs with the same colour and edges in
 * consecutive rows are stacked into rectangles.
 */
#define BAR_COLOR_TOL 90        /* sum of |dR|+|dG|+|dB| from the bar's mean colour;
                                   wide because the units deliver MJPEG, and a solid
                                   red bar reads anywhere from R=121 to R=196 */
#define BAR_CHANNEL_TOL 50      /* and no single channel further off than this */
#define BAR_GAP 16              /* longest gap of glyph pixels inside a bar row */
#define BAR_MIN_WIDTH 48
#define BAR_MIN_HEIGHT 8
#define BAR_MAX_HEIGHT 64
#define BAR_MIN_FILL 0.45f      /* bar-coloured share of a row run */
#define BAR_EDGE_TOL 6          /* run edges may wobble this much between rows */
#define BAR_OUTSIDE_MAX 0.30f   /* max bar-coloured share of the rows above/below */
#define BAR_TEXT_MIN 0.03f      /* glyph pixel share inside the bar */
#define BAR_TEXT_MAX 0.60f
#define BAR_MAX_RUNS 32
#define BAR_MAX_GROUPS 256

typedef struct { int x0, x1; unsigned char c[3]; } BarRun;
typedef struct { int x0, x1, y0, y1; unsigned char c[3]; int open; } BarGroup;

static int bar_close(const unsigned char* p, const unsigned char* c) {
    int dr = abs(p[0] - c[0]), dg = abs(p[1] - c[1]), db = abs(p[2] - c[2]);
    int mx = dr > dg ? dr : dg; if (db > mx) mx = db;
    /* Both limits: the sum alone lets pink glyphs on a red bar pass for the
     * gray panel beside it (255,185,194 vs 184,184,184 sums to 80). */
    return dr + dg + db <= BAR_COLOR_TOL && mx <= BAR_CHANNEL_TOL;
}

/* Row means of one bar still differ a lot under MJPEG (R=186 in one row, 134
 * in the next), so rows are stacked with twice the per-pixel limits. A gray
 * panel and a red bar stay far apart even then. */
static int bar_group_close(const unsigned char* a, const unsigned char* b) {
    int dr = abs(a[0] - b[0]), dg = abs(a[1] - b[1]), db = abs(a[2] - b[2]);
    int mx = dr > dg ? dr : dg; if (db > mx) mx = db;
    return dr + dg + db <= 2 * BAR_COLOR_TOL && mx <= 2 * BAR_CHANNEL_TOL;
}

static int bar_row_runs(const unsigned char* pixels, int width, int y, BarRun* runs) {
    int n = 0, x = 0;
    const unsigned char* row = pixels + (size_t)y * width * 3;
    while (x < width && n < BAR_MAX_RUNS) {
        /* compare against the running mean of the run, not its first pixel */
        long sr = row[x * 3], sg = row[x * 3 + 1], sb = row[x * 3 + 2];
        unsigned char c[3] = { row[x * 3], row[x * 3 + 1], row[x * 3 + 2] };
        int last = x, count = 1;
        for (int j = x + 1; j < width; j++) {
            const unsigned char* p = row + j * 3;
            if (bar_close(p, c)) {
                last = j; count++;
                sr += p[0]; sg += p[1]; sb += p[2];
                c[0] = (unsigned char)(sr / count); c[1] = (unsigned char)(sg / count); c[2] = (unsigned char)(sb / count);
            } else if (j - last > BAR_GAP) break;
        }
        int len = last - x + 1;
        if (len >= BAR_MIN_WIDTH && count >= BAR_MIN_FILL * len) {
            runs[n].x0 = x; runs[n].x1 = last;
            memcpy(runs[n].c, c, 3);
            n++;
            x = last + 1;
        } else {
            x++;
        }
    }
    return n;
}

static float bar_share(const unsigned char* pixels, int width, int height,
                       int y, int x0, int x1, const unsigned char* c) {
    if (y < 0 || y >= height) return 0.0f;
    int hit = 0;
    for (int x = x0; x <= x1; x++)
        if (bar_close(pixels + ((size_t)y * width + x) * 3, c)) hit++;
    return (float)hit / (float)(x1 - x0 + 1);
}

/* OCR the inside of a bar. Ink is what differs clearly in BRIGHTNESS from the
 * bar: the dark JPEG fringes around glyphs on a red bar are merely other
 * shades of red and must not thicken the letters. */
static char* ocr_bar(const unsigned char* pixels, int width, const BarGroup* g) {
    int pad = 4;
    int cw = g->x1 - g->x0 + 1 + 2 * pad, ch = g->y1 - g->y0 + 1 + 2 * pad;
    int nw = cw * OCR_UPSCALE, nh = ch * OCR_UPSCALE;
    float bar_lum = color_luminance(g->c[0], g->c[1], g->c[2]);
    unsigned char* img = malloc((size_t)nw * nh * 3);
    if (!img) return NULL;
    for (int dy = 0; dy < nh; dy++) {
        for (int dx = 0; dx < nw; dx++) {
            int sx = g->x0 - pad + dx / OCR_UPSCALE, sy = g->y0 - pad + dy / OCR_UPSCALE;
            int ink = 0;
            if (sx >= g->x0 && sx <= g->x1 && sy >= g->y0 && sy <= g->y1) {
                const unsigned char* p = pixels + ((size_t)sy * width + sx) * 3;
                ink = fabsf(color_luminance(p[0], p[1], p[2]) - bar_lum) > 60.0f;
            }
            memset(img + ((size_t)dy * nw + dx) * 3, ink ? 0 : 255, 3);
        }
    }
    char base[128], in[160], out[160], cmd[512];
    snprintf(base, sizeof(base), "/tmp/bios_bar_%d_%d_%d", (int)getpid(), g->y0, g->x0);
    snprintf(in, sizeof(in), "%s.png", base);
    snprintf(out, sizeof(out), "%s.txt", base);
    stbi_write_png(in, nw, nh, 3, img, nw * 3);
    free(img);
    snprintf(cmd, sizeof(cmd), "tesseract %s %s --psm %d 2>/dev/null", in, base, TESSERACT_PSM);
    char* text = NULL;
    if (system(cmd) == 0) {
        FILE* f = fopen(out, "r");
        if (f) {
            text = calloc(1, BIOS_MAX_TEXT_LEN);
            if (text && !fgets(text, BIOS_MAX_TEXT_LEN, f)) text[0] = '\0';
            fclose(f);
        }
    }
    unlink(in); unlink(out);
    if (text) {
        size_t len = strlen(text);
        while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == ' ')) text[--len] = '\0';
    }
    return text;
}

static void bar_consider(const unsigned char* pixels, int width, int height,
                         const BarGroup* group, BiosFocusResult* result) {
    /* A bar can reach us in fragments (a glyph row breaks the stacking).
     * Grow the rectangle while the neighbouring row is still mostly the bar
     * colour, so every fragment grows to the same full bar; duplicates are
     * dropped below. */
    BarGroup grown = *group, *g = &grown;
    /* A glyph baseline full of dark JPEG fringes can match as little as 49%,
     * so a row also counts if the one beyond it is clearly bar. */
    while (g->y0 > 0 && g->y1 - g->y0 + 1 <= BAR_MAX_HEIGHT
           && (bar_share(pixels, width, height, g->y0 - 1, g->x0, g->x1, g->c) >= 0.35f
               || bar_share(pixels, width, height, g->y0 - 2, g->x0, g->x1, g->c) >= 0.5f)) g->y0--;
    while (g->y1 < height - 1 && g->y1 - g->y0 + 1 <= BAR_MAX_HEIGHT
           && (bar_share(pixels, width, height, g->y1 + 1, g->x0, g->x1, g->c) >= 0.35f
               || bar_share(pixels, width, height, g->y1 + 2, g->x0, g->x1, g->c) >= 0.5f)) g->y1++;
    for (int i = 0; i < result->num_highlights; i++) {
        const BiosHighlight* o = &result->highlights[i];
        if (o->row >= g->y0 && o->row <= g->y1 && o->col_start <= g->x1 && o->col_end >= g->x0
            && o->confidence > 0.94f) return;    /* this bar is already recorded */
    }
    int w = g->x1 - g->x0 + 1, h = g->y1 - g->y0 + 1;
    if (h < BAR_MIN_HEIGHT || h > BAR_MAX_HEIGHT || w < BAR_MIN_WIDTH || w < 2 * h) return;
    if (w > width * 9 / 10) return;
    if (bar_share(pixels, width, height, g->y0 - 3, g->x0, g->x1, g->c) > BAR_OUTSIDE_MAX) return;
    if (bar_share(pixels, width, height, g->y1 + 3, g->x0, g->x1, g->c) > BAR_OUTSIDE_MAX) return;
    long ink = 0;
    for (int y = g->y0; y <= g->y1; y++)
        for (int x = g->x0; x <= g->x1; x++)
            if (!bar_close(pixels + ((size_t)y * width + x) * 3, g->c)) ink++;
    float ink_share = (float)ink / ((float)w * h);
    if (ink_share < BAR_TEXT_MIN || ink_share > BAR_TEXT_MAX) return;
    if (result->num_highlights >= BIOS_MAX_HIGHLIGHTS) return;

    BiosHighlight* hl = &result->highlights[result->num_highlights++];
    hl->row = (g->y0 + g->y1) / 2;
    hl->col_start = g->x0;
    hl->col_end = g->x1;
    hl->confidence = 0.95f;
    char* t = ocr_bar(pixels, width, g);
    if (t && t[0]) snprintf(hl->text, BIOS_MAX_TEXT_LEN, "%s", t);
    else snprintf(hl->text, BIOS_MAX_TEXT_LEN, "Highlight bar y=%d", hl->row);
    free(t);
}

static int detect_highlight_bars(const unsigned char* pixels, int width, int height,
                                 BiosFocusResult* result) {
    BarGroup* groups = calloc(BAR_MAX_GROUPS, sizeof(BarGroup));
    if (!groups) return -1;
    int ng = 0;
    BarRun runs[BAR_MAX_RUNS];
    for (int y = 0; y <= height; y++) {
        int nr = (y < height) ? bar_row_runs(pixels, width, y, runs) : 0;
        int extended[BAR_MAX_GROUPS] = {0};
        for (int r = 0; r < nr; r++) {
            int hit = -1;
            for (int i = 0; i < ng; i++) {
                BarGroup* g = &groups[i];
                if (g->open && g->y1 == y - 1 && !extended[i]
                    && abs(g->x0 - runs[r].x0) <= BAR_EDGE_TOL
                    && abs(g->x1 - runs[r].x1) <= BAR_EDGE_TOL
                    && bar_group_close(runs[r].c, g->c)) { hit = i; break; }
            }
            if (hit >= 0) {
                BarGroup* g = &groups[hit];
                int rows = g->y1 - g->y0 + 1;   /* keep the group colour a mean of its rows */
                for (int k = 0; k < 3; k++) g->c[k] = (unsigned char)((g->c[k] * rows + runs[r].c[k]) / (rows + 1));
                g->y1 = y;
                extended[hit] = 1;
            } else if (ng < BAR_MAX_GROUPS) {
                BarGroup* g = &groups[ng];
                g->x0 = runs[r].x0; g->x1 = runs[r].x1; g->y0 = g->y1 = y;
                memcpy(g->c, runs[r].c, 3); g->open = 1;
                extended[ng] = 1;
                ng++;
            }
        }
        /* close groups that did not continue into this row */
        for (int i = 0; i < ng; i++) {
            if (groups[i].open && !extended[i]) {
                groups[i].open = 0;
                bar_consider(pixels, width, height, &groups[i], result);
            }
        }
        /* compact closed groups away */
        int k = 0;
        for (int i = 0; i < ng; i++) if (groups[i].open) groups[k++] = groups[i];
        ng = k;
    }
    free(groups);
    return 0;
}

/* Share of letters, digits and spaces in an OCR result: garbage reads low. */
static float text_quality(const char* t) {
    int n = 0, good = 0;
    for (const unsigned char* p = (const unsigned char*)t; *p; p++, n++)
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == ' ')
            good++;
    return n ? (float)good / n : 0.0f;
}

static int by_confidence(const void* a, const void* b) {
    float ca = ((const BiosHighlight*)a)->confidence, cb = ((const BiosHighlight*)b)->confidence;
    return (ca < cb) - (ca > cb);
}

int bios_detect_focus_from_pixels(
    const unsigned char* pixels,
    int width,
    int height,
    BiosFocusResult* result
) {
    if (!pixels || !result) return -1;
    memset(result, 0, sizeof(BiosFocusResult));
    if (detect_highlight_rows(pixels, width, height, result) != 0) return -1;
    /* A focused item is one line (two with a tab bar). Many bright rows mean
     * the screen simply has bright text -- a console log -- not a highlight. */
    int bright_rows = result->num_highlights;
    if (detect_highlight_bars(pixels, width, height, result) != 0) return -1;
    /* Both detectors can fire (and the bright-text one misfires on light
     * frames). Rank: a read that is mostly not text, or no read at all,
     * goes to the back, so highlights[0] is the most credible item. */
    for (int i = 0; i < result->num_highlights; i++) {
        BiosHighlight* hl = &result->highlights[i];
        if (strncmp(hl->text, "Highlight ", 10) == 0 || text_quality(hl->text) < 0.7f
            || (i < bright_rows && bright_rows > 3))
            hl->confidence = 0.3f;
    }
    qsort(result->highlights, result->num_highlights, sizeof(BiosHighlight), by_confidence);
    return 0;
}

int bios_detect_focus(const char* image_path, BiosFocusResult* result) {
    if (!image_path || !result) return -1;

    int width, height, channels;
    unsigned char* pixels = stbi_load(image_path, &width, &height, &channels, 3);
    if (!pixels) {
        fprintf(stderr, "Failed to load image: %s\n", image_path);
        return -1;
    }

    int ret = bios_detect_focus_from_pixels(pixels, width, height, result);
    stbi_image_free(pixels);
    return ret;
}
