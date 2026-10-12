#include "bios_focus_detector.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

#define TEST_IMAGE_DIR "/tmp/openterface/"

int test_bios_image(const char* filename, int expected_highlights) {
    char path[512];
    snprintf(path, sizeof(path), "%s%s", TEST_IMAGE_DIR, filename);

    BiosFocusResult result;
    int ret = bios_detect_focus(path, &result);

    if (ret != 0) {
        printf("FAIL: %s - detection returned error %d\n", filename, ret);
        return 1;
    }

    if (result.num_highlights != expected_highlights) {
        printf("FAIL: %s - expected %d highlights, got %d\n",
               filename, expected_highlights, result.num_highlights);
        return 1;
    }

    printf("PASS: %s - %d highlight(s) detected\n", filename, result.num_highlights);
    return 0;
}

int main(void) {
    int failures = 0;

    printf("=== BIOS Focus Detector Tests ===\n\n");

    printf("Test 1: Non-BIOS (Ubuntu desktop) images\n");
    failures += test_bios_image("openterface_chat_1788777923789.jpg", 0);
    failures += test_bios_image("openterface_chat_1788779481242.jpg", 0);

    printf("\nTest 2: BIOS images with highlighted items\n");
    failures += test_bios_image("openterface_chat_1788781262279.jpg", 1);
    failures += test_bios_image("openterface_chat_1788781277678.jpg", 1);
    failures += test_bios_image("openterface_chat_1788781290417.jpg", 1);
    failures += test_bios_image("openterface_chat_1788781300742.jpg", 1);
    failures += test_bios_image("openterface_chat_1788781315417.jpg", 1);
    failures += test_bios_image("openterface_chat_1788781332202.jpg", 1);

    printf("\nTest 3: Non-BIOS images should return 0 highlights\n");
    failures += test_bios_image("openterface_chat_1788781202464.jpg", 0);
    failures += test_bios_image("openterface_chat_1788781217414.jpg", 0);

    printf("\nTest 4: Verify highlight details\n");
    {
        BiosFocusResult result;
        int ret = bios_detect_focus(TEST_IMAGE_DIR "openterface_chat_1788781262279.jpg", &result);
        if (ret == 0 && result.num_highlights == 1) {
            BiosHighlight* hl = &result.highlights[0];
            if (hl->row >= 550 && hl->row <= 650) {
                printf("PASS: Highlight row in expected range (%d)\n", hl->row);
            } else {
                printf("FAIL: Highlight row out of range (%d)\n", hl->row);
                failures++;
            }
            if (hl->confidence > 0.8f) {
                printf("PASS: Confidence above threshold (%.2f)\n", hl->confidence);
            } else {
                printf("FAIL: Confidence too low (%.2f)\n", hl->confidence);
                failures++;
            }
            if (strstr(hl->text, "System Language")) {
                printf("PASS: OCR extracted text contains 'System Language': %s\n", hl->text);
            } else {
                printf("FAIL: OCR did not extract expected text: %s\n", hl->text);
                failures++;
            }
            if (strstr(hl->text, "[English]")) {
                printf("PASS: OCR extracted text contains '[English]': %s\n", hl->text);
            } else {
                printf("FAIL: OCR did not extract '[English]': %s\n", hl->text);
                failures++;
            }
        }
    }

    printf("\n=== Summary: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
