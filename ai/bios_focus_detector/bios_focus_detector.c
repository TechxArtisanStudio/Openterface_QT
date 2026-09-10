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
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/bios_ocr_%d", (int)(y * 1000 + x_start));
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

int bios_detect_focus_from_pixels(
    const unsigned char* pixels,
    int width,
    int height,
    BiosFocusResult* result
) {
    if (!pixels || !result) return -1;
    memset(result, 0, sizeof(BiosFocusResult));
    return detect_highlight_rows(pixels, width, height, result);
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
