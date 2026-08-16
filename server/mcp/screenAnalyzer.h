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
     * @return ScreenAnalysis result with detected elements and Markdown output
     */
    ScreenAnalysis analyzeScreen(const QImage& frame, const QString& detailLevel = "detailed");

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
};

#endif // SCREEN_ANALYZER_H
