/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/

#ifndef SCREEN_ANALYZER_H
#define SCREEN_ANALYZER_H

#include <QImage>
#include <QString>
#include <QList>
#include <QRect>

// Forward declarations
namespace tesseract {
    class TessBaseAPI;
}
#ifdef HAVE_OPENCV
namespace cv {
    class Mat;
}
#endif

/**
 * Represents a detected text element on the screen with its position and confidence.
 */
struct TextElement {
    QString text;           // Detected text content
    QRect boundingBox;      // Bounding box in pixel coordinates
    int pixelX;             // X coordinate in pixels
    int pixelY;             // Y coordinate in pixels
    int mcpX;               // X coordinate in MCP range (0-4096)
    int mcpY;               // Y coordinate in MCP range (0-4096)
    float confidence;       // OCR confidence (0.0 to 1.0)
};

/**
 * Represents a detected UI element (button, menu item, text field, etc.)
 */
struct UIElement {
    QString type;           // "button", "menu", "textfield", "label", etc.
    QString text;           // Text content if any
    int pixelX;             // X coordinate in pixels
    int pixelY;             // Y coordinate in pixels
    int mcpX;               // X coordinate in MCP range (0-4096)
    int mcpY;               // Y coordinate in MCP range (0-4096)
    QRect bounds;           // Bounding box in pixel coordinates
};

/**
 * Complete analysis result of a screen capture.
 */
struct ScreenAnalysis {
    int screenWidth;                    // Screen width in pixels
    int screenHeight;                   // Screen height in pixels
    QList<TextElement> textElements;    // All detected text elements
    QList<UIElement> uiElements;        // Detected UI elements
    QString markdownOutput;             // Generated Markdown representation
};

/**
 * Result of terminal idle detection.
 * Combines multiple signals to determine if a terminal is waiting for user input:
 *   1. Cursor blink detection (temporal differencing)
 *   2. Screen stability (total change ratio between frames)
 *   3. Shell prompt detection (OCR on last line)
 */
struct CursorDetectionResult {
    bool detected;              // true if terminal appears to be idle/waiting
    QRect position;             // Bounding box of the cursor in pixel coordinates (if detected)
    int mcpX;                   // Cursor X in MCP range (0-4096)
    int mcpY;                   // Cursor Y in MCP range (0-4096)
    float confidence;           // Combined confidence (0.0 to 1.0)
    QString status;             // "idle", "likely_idle", "outputting", "unknown"
    QString description;        // Human-readable explanation

    // Individual signal details (for debugging / advanced consumers)
    bool cursorBlinkDetected;   // true if a blinking cursor was found
    bool screenStable;          // true if screen changes are below threshold
    float totalChangeRatio;     // total fraction of screen that changed between frames
    bool promptDetected;        // true if a shell prompt pattern was found on last line
    QString promptText;         // the detected prompt text (if any)
};

/**
 * Analysis mode for screen OCR.
 * General mode is optimized for UI elements with coordinates.
 * Terminal mode is optimized for monospaced command output.
 */
enum class AnalysisMode {
    General,   // UI text with coordinates and element detection
    Terminal   // Terminal/command output with preserved layout
};

/**
 * A small region that changed between two consecutive frames.
 * Used internally by cursor blink detection to track which parts of the
 * screen are changing over time.
 */
struct ChangeRegion {
    QRect rect;               // Bounding box of the changed region
    int pixelCount;           // Number of changed pixels
    float ratio;              // Fraction of total screen that changed
    QPointF centroid;         // Center point of the changed region
};

/**
 * Screen analyzer that uses OCR to extract text and UI elements from screen captures.
 * Converts the screen to a structured Markdown representation for AI consumption.
 */
class ScreenAnalyzer {
public:
    ScreenAnalyzer();
    ~ScreenAnalyzer();

    /**
     * Analyze a screen image and extract text/UI elements with coordinates.
     * @param frame The screen image to analyze
     * @param detailLevel "basic" or "detailed" - controls output verbosity
     * @param mode General (UI) or Terminal (command output) - affects preprocessing and output format
     * @return ScreenAnalysis result with detected elements and Markdown output
     */
    ScreenAnalysis analyzeScreen(const QImage& frame, const QString& detailLevel = "detailed",
                                 AnalysisMode mode = AnalysisMode::General);

    /**
     * Detect a blinking terminal cursor from a sequence of pre-captured frames.
     *
     * This uses temporal differencing: it analyzes pixel diffs between consecutive
     * frames and checks whether the same small region changed in multiple diffs.
     * A blinking cursor is the only thing that changes in an idle terminal, so if
     * the only change is a small (~character-sized) region toggling at a fixed
     * position, the terminal is waiting for input.
     *
     * The caller is responsible for capturing frames at appropriate intervals
     * (typically 350ms apart, 5 frames total) and passing at least 3 frames.
     * 350ms avoids phase-locking with common 500ms/1000ms/1200ms blink periods,
     * and 5 frames gives 4 diffs over 1.4s — enough to catch slow blink rates.
     *
     * Algorithm overview:
     * 1. Compute diffs between consecutive frame pairs
     * 2. For each diff, find small changed regions (cursor-sized)
     * 3. Check if the same position changed in multiple consecutive diffs
     *    (this distinguishes a blinking cursor from new terminal output)
     * 4. If a stable position toggles across 2+ diffs → cursor detected
     *
     * @param frames List of frames captured at regular intervals (min 3, at ~500ms apart)
     * @return CursorDetectionResult with detection status, position, and confidence
     */
    CursorDetectionResult detectCursorFromFrames(const QList<QImage>& frames);

    /**
     * Check if Tesseract OCR is properly initialized and available.
     * @return true if OCR is ready to use
     */
    bool isAvailable() const;

    /**
     * Check if OpenCV is available for visual analysis.
     * @return true if OpenCV was compiled in
     */
    bool isOpenCVAvailable() const;

private:
    tesseract::TessBaseAPI* m_tesseract;
    bool m_initialized;
    QImage m_previousFrame;   // Stored for differential OCR (change detection)
#ifdef HAVE_OPENCV
    bool m_opencvAvailable;
#endif

    /**
     * Initialize Tesseract OCR engine.
     * @return true if initialization succeeded
     */
    bool initializeTesseract();

    /**
     * Extract text elements with bounding boxes from the image.
     * @param frame The image to process
     * @return List of detected text elements with coordinates
     */
    QList<TextElement> extractTextWithPositions(const QImage& frame);

    /**
     * Extract terminal text using OCR optimized for command output.
     * Uses preprocessing and Tesseract settings tuned for monospaced text.
     * @param frame The terminal image to process
     * @return Plain text with preserved terminal layout
     */
    QString extractTerminalText(const QImage& frame);

    /**
     * Detect the region that changed between the current frame and the previous frame.
     * Uses OpenCV absdiff to find pixel differences, then computes the bounding box
     * of all changed areas. Useful for differential OCR — only processing the part
     * of the screen that actually changed (e.g. new terminal output).
     * @param currentFrame The current screen capture
     * @param changedRect Output: bounding box of all changed pixels (in currentFrame coords)
     * @param changeRatio Output: fraction of pixels that changed (0.0 to 1.0)
     * @return true if a previous frame exists for comparison, false if this is the first frame
     */
    bool detectChangedRegion(const QImage& currentFrame, QRect& changedRect, float& changeRatio);

    /**
     * Store the current frame for next call's diff comparison.
     * Called automatically after each terminal OCR.
     */
    void updatePreviousFrame(const QImage& frame);

    /**
     * Clear the stored previous frame (e.g. when starting a new session).
     */
    void clearPreviousFrame();

    /**
     * Convert detected text elements into UI elements (buttons, menus, etc.)
     * @param textElements List of detected text elements
     * @param screenWidth Screen width for coordinate conversion
     * @param screenHeight Screen height for coordinate conversion
     * @return List of detected UI elements
     */
    QList<UIElement> detectUIElements(const QList<TextElement>& textElements,
                                      int screenWidth, int screenHeight);

#ifdef HAVE_OPENCV
    /**
     * Detect button regions using OpenCV visual analysis.
     * Uses edge detection, contour analysis, and color segmentation
     * to find rectangular regions that look like buttons.
     * @param frame The image to analyze
     * @param textElements Existing text elements for correlation
     * @return List of detected button regions as UIElements
     */
    QList<UIElement> detectButtonsVisually(const QImage& frame,
                                           const QList<TextElement>& textElements);

    /**
     * Preprocess image for terminal OCR.
     * Applies grayscale conversion, adaptive thresholding, sharpening,
     * and optional upscaling to improve terminal text recognition.
     * @param frame The input image
     * @return Preprocessed QImage optimized for terminal OCR
     */
    QImage preprocessForTerminal(const QImage& frame);

    /**
     * Convert QImage to cv::Mat for OpenCV processing.
     * @param image The QImage to convert
     * @return OpenCV Mat in BGR format
     */
    cv::Mat QImageToMat(const QImage& image);
#endif

    /**
     * Generate structured Markdown from analysis results.
     * @param analysis The analysis result to format
     * @param detailLevel "basic" or "detailed"
     * @return Markdown-formatted string
     */
    QString generateMarkdown(const ScreenAnalysis& analysis, const QString& detailLevel);

    /**
     * Generate terminal-friendly Markdown from extracted text.
     * Preserves terminal layout and uses code block formatting.
     * @param terminalText The extracted terminal text
     * @param screenWidth Screen width for header
     * @param screenHeight Screen height for header
     * @param changedRect Optional: the changed region (for differential OCR context)
     * @return Markdown-formatted string with terminal content
     */
    QString generateTerminalMarkdown(const QString& terminalText, int screenWidth, int screenHeight,
                                     const QRect* changedRect = nullptr);

    /**
     * Convert pixel coordinates to MCP coordinates (0-4096 range).
     * @param pixelX X coordinate in pixels
     * @param pixelY Y coordinate in pixels
     * @param screenWidth Screen width in pixels
     * @param screenHeight Screen height in pixels
     * @param mcpX Output: X coordinate in MCP range
     * @param mcpY Output: Y coordinate in MCP range
     */
    void convertToMCPCoordinates(int pixelX, int pixelY,
                                 int screenWidth, int screenHeight,
                                 int& mcpX, int& mcpY);

    // --- Cursor blink detection helpers ---

    /**
     * Find small changed regions between two frames.
     * Uses cv::absdiff + threshold + contour detection to locate regions
     * that changed between the two frames. Only returns regions that are
     * small enough to be cursor-sized (filters out large content changes).
     *
     * @param frameA First frame
     * @param frameB Second frame
     * @param maxRatio Maximum change ratio to consider (regions larger than
     *                 this fraction of the screen are ignored)
     * @return List of small change regions with centroids
     */
    QList<ChangeRegion> findSmallChanges(const QImage& frameA, const QImage& frameB,
                                          float maxRatio = 0.02f);

    /**
     * Check if two change regions are at approximately the same screen position.
     * Used to determine if a cursor-sized change is toggling at a fixed location.
     *
     * @param a First change region
     * @param b Second change region
     * @param tolerance Maximum pixel distance between centroids to consider "same position"
     * @return true if the regions are at approximately the same position
     */
    bool isSamePosition(const ChangeRegion& a, const ChangeRegion& b, float tolerance = 30.0f);

    /**
     * Detect a shell prompt on the last line of the terminal via OCR.
     * Looks for common prompt patterns: "$ ", "# ", "> ", "% ", "~$ ", etc.
     *
     * @param frame The terminal screen image
     * @param promptText Output: the detected prompt text if found
     * @return true if a shell prompt pattern was detected on the last line
     */
    bool detectShellPrompt(const QImage& frame, QString& promptText);
};

#endif // SCREEN_ANALYZER_H
