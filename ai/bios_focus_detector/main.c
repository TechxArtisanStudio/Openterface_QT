#include "bios_focus_detector.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <image_path>\n", argv[0]);
        return 1;
    }

    BiosFocusResult result;
    int ret = bios_detect_focus(argv[1], &result);
    if (ret != 0) {
        fprintf(stderr, "Detection failed\n");
        return 1;
    }

    printf("Grid size: %dx%d\n", result.grid_width, result.grid_height);
    printf("Number of highlights: %d\n", result.num_highlights);
    printf("\nHighlights:\n");

    for (int i = 0; i < result.num_highlights; i++) {
        BiosHighlight* hl = &result.highlights[i];
        printf("  [%d] Row %d, Cols %d-%d\n", i, hl->row, hl->col_start, hl->col_end);
        printf("       Text: %s\n", hl->text);
        printf("       Length: %zu chars\n", strlen(hl->text));
        printf("       Confidence: %.2f\n", hl->confidence);
        printf("\n");
    }

    return 0;
}
