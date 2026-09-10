# BIOS Focus Detector

C library for detecting highlighted/selected menu items in BIOS/UEFI screenshots.

## Purpose

BIOS screens use reverse-video / bright foreground for the active item, e.g.:
- **Aptio (AMI)**: normal text = blue on gray, highlighted = white on gray
- **Phoenix/Award**: normal text = dim white on dark blue, highlighted = bright white on dark blue
- **Other UEFI BIOSes**: typically any scheme where the focused item has brighter text than surrounding items

The detector works with any BIOS color scheme where the focused item is rendered as **brighter text** (luminance > 210) on a consistent background. It dynamically detects the screen's background color via edge sampling, so it's not limited to gray backgrounds.

The detector finds those bright-text rows, estimates their pixel bounds and extracts the text via Tesseract OCR. The result is usable by the Openterface Qt app to report the current BIOS selection in markdown and coordinate space.

## Files

- `bios_focus_detector.h` – public C API
- `bios_focus_detector.c` – implementation
- `stb_image.h`, `stb_image_write.h` – vendored image I/O
- `vga_font.c/h` – optional VGA font data
- `test_bios_focus_detector.c` – unit tests
- `Makefile` – standalone build

## API

```c
#define BIOS_MAX_HIGHLIGHTS 32
#define BIOS_MAX_TEXT_LEN 256

typedef struct { int row; int col_start; int col_end; char text[BIOS_MAX_TEXT_LEN]; float confidence; } BiosHighlight;
typedef struct { int grid_width; int grid_height; int num_highlights; BiosHighlight highlights[BIOS_MAX_HIGHLIGHTS]; } BiosFocusResult;

int bios_detect_focus(const char* image_path, BiosFocusResult* result);
int bios_detect_focus_from_pixels(const unsigned char* pixels, int width, int height, BiosFocusResult* result);
```

* `bios_detect_focus` loads BMP/JPEG/PNG via stb_image and runs detection.
* `bios_detect_focus_from_pixels` works on raw RGB24 buffers – used by Qt integration.

Return 0 on success, -1 on error. `result.num_highlights` is the count of detected rows.

## Algorithm

1. Convert image to per-pixel luminance: `0.2126R + 0.7152G + 0.0722B`
2. Exclude title/footer: top 8% / bottom 8% of image
3. **Dynamic background detection**: sample the leftmost/rightmost 5% of pixels in the content area (edge sampling). Menu items typically start after the edge margin, so edge pixels are almost always background. This gives `screen_bg_lum` which works for any BIOS color scheme.
4. Reject near-black (lum < 10) or near-white (lum > 245) screens (likely not a BIOS menu)
5. Per-row: count "bright" pixels (lum > 210) and compute row background via edge sampling
6. Group consecutive rows with bright count > `MIN_BRIGHT_PIXELS` (20), require height >= `MIN_GROUP_HEIGHT` (8)
7. For each group, check that row's bg luminance is within `BG_TOLERANCE` (50) of `screen_bg_lum` → candidate
8. For each candidate group, compute mid-y, first/last bright x, set confidence 0.9
9. Crop around the row and upscale 3×, threshold to black-on-white (keep only lum > 200 as black), run `tesseract --psm 7` on the crop to extract text
10. Return row, col_start, col_end, text, confidence

Tuning constants:
- `LUMINANCE_WHITE_THRESHOLD = 210.0f` — minimum luminance for "bright" (focused) text
- `BG_EDGE_MARGIN_RATIO = 0.05f` — fraction of width sampled from each edge for bg detection
- `BG_TOLERANCE = 50` — max difference between row bg and screen bg
- `OCR_THRESHOLD = 200` — OCR preprocessing threshold
- `OCR_UPSCALE = 3` — upscale factor before OCR

## Supported BIOS Types

| BIOS Type | Background | Normal Text | Focused Text | Supported |
|-----------|-----------|-------------|--------------|-----------|
| Aptio (AMI) | Gray (~164) | Blue (~32) | White (~250) | ✅ |
| Phoenix/Award | Dark blue (~40) | Dim white (~120) | Bright white (~250) | ✅ |
| Other UEFI with bright-text focus | Any | Darker | Bright (lum > 210) | ✅ |
| BIOSes with bg-color-inversion focus | Varies | Same bg | Different bg | ❌ (future) |
| Very old text-mode BIOS | Varies | Attribute bytes | Attribute bytes | ❌ (different approach needed) |

## Qt Integration

The detector is compiled into `openterfaceQT` under `ai/bios_focus_detector/`.

`server/mcp/screenAnalyzer.cpp`:
- Includes `bios_focus_detector.h`
- In `ScreenAnalyzer::analyzeScreen` General mode, converts `QImage` to RGB888, copies to a tightly packed buffer and calls `bios_detect_focus_from_pixels`.
- Each highlight is mapped to a `TextElement` with:
  * `text` = OCR text
  * `pixelX/pixelY` = centre of highlight
  * `boundingBox` = `QRect(col_start, row, col_end-col_start, 30)`
  * `confidence` = highlight confidence
  * MCP coordinates via `convertToMCPCoordinates`

The resulting elements are added to `ScreenAnalysis.textElements` and flow into the generated Markdown.

## Building standalone

```bash
cd ai/bios_focus_detector
make
make test
```

Requires `tesseract` in PATH for OCR tests.

## Test data

Test images live in `/tmp/openterface/`:
- Non-BIOS: `openterface_chat_1788777923789.jpg`, `openterface_chat_1788779481242.jpg`
- BIOS with highlight: `openterface_chat_1788781262279.jpg` etc.

All 12 tests pass with 0 failures.

## Notes

- Detection is deliberately conservative to avoid false positives on desktop UIs.
- OCR per highlight uses a `system("tesseract ...")` call with temporary PNG/TXT files cleaned up after use.
- For production use consider replacing the system call with libtesseract to avoid process spawn overhead.
