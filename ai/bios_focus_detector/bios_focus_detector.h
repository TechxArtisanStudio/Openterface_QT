#ifndef BIOS_FOCUS_DETECTOR_H
#define BIOS_FOCUS_DETECTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#define BIOS_MAX_HIGHLIGHTS 32
#define BIOS_MAX_TEXT_LEN 256

typedef struct {
    int row;
    int col_start;
    int col_end;
    char text[BIOS_MAX_TEXT_LEN];
    float confidence;
} BiosHighlight;

typedef struct {
    int grid_width;
    int grid_height;
    int num_highlights;
    BiosHighlight highlights[BIOS_MAX_HIGHLIGHTS];
} BiosFocusResult;

/**
 * Detect highlighted/selected items in a BIOS screenshot
 * @param image_path Path to image file (BMP/JPEG/PNG)
 * @param result Output structure containing detected highlights
 * @return 0 on success, -1 on error
 */
int bios_detect_focus(const char* image_path, BiosFocusResult* result);

/**
 * Detect focused item from raw pixel data
 * @param pixels RGB24 pixel data
 * @param width Image width in pixels
 * @param height Image height in pixels
 * @param result Output structure containing detected highlights
 * @return 0 on success, -1 on error
 */
int bios_detect_focus_from_pixels(const unsigned char* pixels, int width, int height, BiosFocusResult* result);

#ifdef __cplusplus
}
#endif

#endif // BIOS_FOCUS_DETECTOR_H
