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

#include "screenAnalyzer.h"
#include <QLoggingCategory>
#include <QDebug>
#include <QRegularExpression>

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

#ifdef HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#if CV_VERSION_MAJOR >= 5
// OpenCV 5 moved cv::boundingRect(InputArray) from imgproc to the new
// opencv2/geometry module, and imgproc.hpp no longer provides it
// transitively. The module does not exist in OpenCV 4, hence the guard.
#include <opencv2/geometry/2d.hpp>
#endif
#endif

Q_LOGGING_CATEGORY(log_screen_analyzer, "opf.screen.analyzer")

ScreenAnalyzer::ScreenAnalyzer()
    : m_tesseract(nullptr)
    , m_initialized(false)
#ifdef HAVE_OPENCV
    , m_opencvAvailable(true)
#endif
{
#ifdef HAVE_TESSERACT
    m_initialized = initializeTesseract();
#else
    qCWarning(log_screen_analyzer) << "Tesseract OCR not available - screen_to_markdown will not work";
#endif

#ifdef HAVE_OPENCV
    qCInfo(log_screen_analyzer) << "OpenCV visual analysis enabled";
#else
    qCInfo(log_screen_analyzer) << "OpenCV not available - visual button detection disabled";
#endif
}

ScreenAnalyzer::~ScreenAnalyzer()
{
#ifdef HAVE_TESSERACT
    if (m_tesseract) {
        m_tesseract->End();
        delete m_tesseract;
        m_tesseract = nullptr;
    }
#endif
}

bool ScreenAnalyzer::isAvailable() const
{
    return m_initialized;
}

bool ScreenAnalyzer::isOpenCVAvailable() const
{
#ifdef HAVE_OPENCV
    return m_opencvAvailable;
#else
    return false;
#endif
}

bool ScreenAnalyzer::initializeTesseract()
{
#ifdef HAVE_TESSERACT
    m_tesseract = new tesseract::TessBaseAPI();

    // Initialize with English language
    // First parameter is the data path (usually TESSDATA_PREFIX env var)
    // Second parameter is the language code
    if (m_tesseract->Init(nullptr, "eng") != 0) {
        qCCritical(log_screen_analyzer) << "Failed to initialize Tesseract OCR";
        delete m_tesseract;
        m_tesseract = nullptr;
        return false;
    }

    // Set page segmentation mode to auto
    m_tesseract->SetPageSegMode(tesseract::PSM_AUTO);

    qCInfo(log_screen_analyzer) << "Tesseract OCR initialized successfully";
    return true;
#else
    return false;
#endif
}

ScreenAnalysis ScreenAnalyzer::analyzeScreen(const QImage& frame, const QString& detailLevel,
                                             AnalysisMode mode)
{
    ScreenAnalysis result;
    result.screenWidth = frame.width();
    result.screenHeight = frame.height();

    if (!m_initialized) {
        qCWarning(log_screen_analyzer) << "ScreenAnalyzer not initialized";
        result.markdownOutput = "# Error\n\nOCR engine not available. Tesseract may not be installed.\n";
        return result;
    }

    if (frame.isNull()) {
        qCWarning(log_screen_analyzer) << "Cannot analyze null frame";
        result.markdownOutput = "# Error\n\nNo image available for analysis.\n";
        return result;
    }

    qCInfo(log_screen_analyzer) << "Analyzing screen" << result.screenWidth << "x" << result.screenHeight
                                << "mode:" << (mode == AnalysisMode::Terminal ? "Terminal" : "General");

    // Terminal mode: use specialized OCR for command output
    if (mode == AnalysisMode::Terminal) {
        qCInfo(log_screen_analyzer) << "Using terminal OCR mode";

        // Detect changed region for differential OCR
        QRect changedRect;
        float changeRatio = 0.0f;
        bool hasPrevious = detectChangedRegion(frame, changedRect, changeRatio);

        QImage ocrRegion = frame;  // Default: full frame
        bool usingRegion = false;

        if (hasPrevious && changeRatio > 0.001f && changeRatio < 0.85f) {
            // Significant change but not the whole screen — crop to changed area
            // Add a small padding (20px) around the changed region to capture
            // context lines above/below the change
            int pad = 20;
            int x1 = qMax(0, changedRect.x() - pad);
            int y1 = qMax(0, changedRect.y() - pad);
            int x2 = qMin(frame.width(), changedRect.right() + 1 + pad);
            int y2 = qMin(frame.height(), changedRect.bottom() + 1 + pad);
            ocrRegion = frame.copy(x1, y1, x2 - x1, y2 - y1);
            usingRegion = true;
            qCInfo(log_screen_analyzer) << "Differential OCR: changeRatio=" << changeRatio
                                        << "region=" << changedRect
                                        << "padded=" << ocrRegion.width() << "x" << ocrRegion.height();
        } else if (hasPrevious) {
            qCInfo(log_screen_analyzer) << "Differential OCR: no significant change (ratio="
                                        << changeRatio << "), using full frame";
        }

        QString terminalText = extractTerminalText(ocrRegion);

        // Update stored frame for next diff
        updatePreviousFrame(frame);

        result.markdownOutput = generateTerminalMarkdown(terminalText, result.screenWidth, result.screenHeight,
                                                          usingRegion ? &changedRect : nullptr);
        return result;
    }

    // General mode: extract text elements with positions for UI analysis
    qCInfo(log_screen_analyzer) << "Using general OCR mode";
    result.textElements = extractTextWithPositions(frame);
    qCInfo(log_screen_analyzer) << "Detected" << result.textElements.size() << "text elements";

    // Detect UI elements from text
    result.uiElements = detectUIElements(result.textElements, result.screenWidth, result.screenHeight);

    // Detect buttons visually using OpenCV
#ifdef HAVE_OPENCV
    if (m_opencvAvailable) {
        QList<UIElement> visualButtons = detectButtonsVisually(frame, result.textElements);
        qCInfo(log_screen_analyzer) << "OpenCV detected" << visualButtons.size() << "visual button regions";

        // Merge visual detections with text-based detections
        // Only add visual buttons that don't overlap with existing text-based buttons
        for (const auto& visualBtn : visualButtons) {
            bool alreadyDetected = false;
            for (const auto& existing : result.uiElements) {
                if (existing.type == "button" && existing.bounds.intersects(visualBtn.bounds)) {
                    alreadyDetected = true;
                    break;
                }
            }
            if (!alreadyDetected) {
                result.uiElements.append(visualBtn);
            }
        }
    }
#endif

    qCInfo(log_screen_analyzer) << "Total UI elements:" << result.uiElements.size();

    // Generate Markdown output
    result.markdownOutput = generateMarkdown(result, detailLevel);

    // Keep the stored frame fresh even in general mode, so a subsequent terminal
    // mode call doesn't compare against a stale frame from many iterations ago.
    updatePreviousFrame(frame);

    return result;
}

QList<TextElement> ScreenAnalyzer::extractTextWithPositions(const QImage& frame)
{
    QList<TextElement> elements;

#ifdef HAVE_TESSERACT
    if (!m_tesseract) {
        return elements;
    }

    // Convert QImage to format suitable for Tesseract
    // Tesseract expects 8-bit grayscale or 32-bit color
    QImage converted = frame.convertToFormat(QImage::Format_RGB32);

    // Set the image data
    m_tesseract->SetImage(
        converted.constBits(),
        converted.width(),
        converted.height(),
        4,  // bytes per pixel (RGB32 = 4 bytes)
        converted.bytesPerLine()
    );

    // Get OCR results with bounding boxes
    m_tesseract->Recognize(nullptr);
    tesseract::ResultIterator* ri = m_tesseract->GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_WORD;

    while (ri != nullptr) {
        if (ri->IsAtBeginningOf(level)) {
            int left, top, right, bottom;
            ri->BoundingBox(level, &left, &top, &right, &bottom);

            const char* word = ri->GetUTF8Text(level);
            float confidence = ri->Confidence(level);

            if (word && strlen(word) > 0) {
                TextElement elem;
                elem.text = QString::fromUtf8(word);
                elem.boundingBox = QRect(left, top, right - left, bottom - top);
                elem.confidence = confidence / 100.0f;  // Convert to 0.0-1.0

                // Calculate center point
                int centerX = left + (right - left) / 2;
                int centerY = top + (bottom - top) / 2;

                elem.pixelX = centerX;
                elem.pixelY = centerY;

                convertToMCPCoordinates(
                    centerX, centerY,
                    frame.width(), frame.height(),
                    elem.mcpX, elem.mcpY
                );

                // Filter out low confidence detections
                if (elem.confidence > 0.5f) {
                    elements.append(elem);
                }
            }

            delete[] word;
        }

        // Move to next word
        if (!ri->Next(level)) {
            break;
        }
    }

    delete ri;
#endif

    return elements;
}

QList<UIElement> ScreenAnalyzer::detectUIElements(const QList<TextElement>& textElements,
                                                   int screenWidth, int screenHeight)
{
    QList<UIElement> uiElements;

    // Button text patterns (case-insensitive)
    static const QStringList buttonKeywords = {
        "ok", "cancel", "yes", "no", "submit", "save", "delete", "remove",
        "apply", "close", "confirm", "accept", "decline", "reject",
        "next", "back", "finish", "done", "continue", "skip",
        "login", "sign in", "signin", "register", "signup", "sign up",
        "logout", "sign out", "signout",
        "restart", "shutdown", "power", "reboot", "sleep", "hibernate",
        "button", "click", "tap", "press",
        "minimize", "maximize", "restore", "close",
        "search", "find", "filter", "sort", "refresh", "reload",
        "add", "new", "create", "edit", "update", "modify",
        "upload", "download", "import", "export", "share", "send",
        "play", "pause", "stop", "record", "mute", "volume",
        "more", "less", "show", "hide", "expand", "collapse",
        "help", "info", "settings", "preferences", "options", "config",
        "install", "uninstall", "update", "upgrade", "cancel"
    };

    // Menu text patterns
    static const QStringList menuKeywords = {
        "menu", "file", "edit", "view", "tools", "help", "window",
        "format", "insert", "data", "options", "preferences",
        "recent", "open", "close", "exit", "quit"
    };

    // Link patterns
    static const QStringList linkKeywords = {
        "here", "link", "more info", "details", "read more", "learn more",
        "not listed?", "forgot", "reset", "register", "sign up"
    };

    for (const auto& textElem : textElements) {
        UIElement uiElem;
        uiElem.text = textElem.text;
        uiElem.pixelX = textElem.pixelX;
        uiElem.pixelY = textElem.pixelY;
        uiElem.mcpX = textElem.mcpX;
        uiElem.mcpY = textElem.mcpY;
        uiElem.bounds = textElem.boundingBox;

        QString textLower = textElem.text.toLower().trimmed();
        int textLength = textElem.text.length();
        int pixelY = textElem.boundingBox.bottom();

        // Check if text matches button keywords
        bool isButtonKeyword = false;
        for (const QString& keyword : buttonKeywords) {
            if (textLower == keyword || textLower.contains(keyword)) {
                isButtonKeyword = true;
                break;
            }
        }

        // Check if text matches menu keywords
        bool isMenuKeyword = false;
        for (const QString& keyword : menuKeywords) {
            if (textLower == keyword || textLower.contains(keyword)) {
                isMenuKeyword = true;
                break;
            }
        }

        // Check if text matches link keywords
        bool isLinkKeyword = false;
        for (const QString& keyword : linkKeywords) {
            if (textLower == keyword || textLower.contains(keyword)) {
                isLinkKeyword = true;
                break;
            }
        }

        // Position-based heuristics for buttons
        bool isBottomPosition = (pixelY > screenHeight * 0.75);
        bool isTopPosition = (pixelY < screenHeight * 0.1);
        bool isShortText = (textLength > 0 && textLength < 25);
        bool isHighConfidence = (textElem.confidence > 0.75);

        // Classification logic
        if (isButtonKeyword) {
            uiElem.type = "button";
        } else if (isMenuKeyword && isTopPosition) {
            uiElem.type = "menu";
        } else if (isLinkKeyword) {
            uiElem.type = "link";
        } else if (isBottomPosition && isShortText && isHighConfidence) {
            // Text at bottom of screen, short, and high confidence - likely a button
            uiElem.type = "button";
        } else if (isShortText && isHighConfidence && textLower.length() < 15) {
            // Short, high-confidence text - could be interactive
            uiElem.type = "button";
        } else {
            uiElem.type = "text";
        }

        uiElements.append(uiElem);
    }

    return uiElements;
}

QString ScreenAnalyzer::generateMarkdown(const ScreenAnalysis& analysis, const QString& detailLevel)
{
    QString md;

    // Header
    md += QString("# Screen Layout (%1x%2)\n\n").arg(analysis.screenWidth).arg(analysis.screenHeight);

    if (detailLevel == "basic") {
        // Basic mode: just list interactive elements
        md += "## Interactive Elements\n\n";
        md += "> Use MCP coordinates with `mouse_click` tool\n\n";

        if (analysis.uiElements.isEmpty()) {
            md += "_No interactive elements detected._\n";
        } else {
            for (const auto& elem : analysis.uiElements) {
                if (elem.type == "button" || elem.type == "menu" || elem.type == "link") {
                    md += QString("- **[%1]** pixel(%2, %3) MCP(%4, %5) - %6\n")
                        .arg(elem.text)
                        .arg(elem.pixelX)
                        .arg(elem.pixelY)
                        .arg(elem.mcpX)
                        .arg(elem.mcpY)
                        .arg(elem.type.toUpper());
                }
            }
        }
    } else {
        // Detailed mode: full breakdown
        md += "## Detected Text Elements\n\n";
        md += "> **Coordinate formats:** Pixel (actual screen position) | MCP (0-4096 range, for mouse_click tool)\n\n";

        if (analysis.textElements.isEmpty()) {
            md += "_No text detected on screen._\n\n";
        } else {
            md += "| Text | Pixel Coords | MCP Coords | Confidence |\n";
            md += "|------|--------------|------------|------------|\n";

            for (const auto& elem : analysis.textElements) {
                md += QString("| %1 | (%2, %3) | (%4, %5) | %6 |\n")
                    .arg(elem.text)
                    .arg(elem.pixelX)
                    .arg(elem.pixelY)
                    .arg(elem.mcpX)
                    .arg(elem.mcpY)
                    .arg(elem.confidence, 0, 'f', 2);
            }
            md += "\n";
        }

        md += "## Interactive Elements\n\n";

        if (analysis.uiElements.isEmpty()) {
            md += "_No interactive elements detected._\n\n";
        } else {
            // Group by type
            QMap<QString, QList<UIElement>> byType;
            for (const auto& elem : analysis.uiElements) {
                byType[elem.type].append(elem);
            }

            for (auto it = byType.begin(); it != byType.end(); ++it) {
                md += QString("### %1\n\n").arg(it.key().toUpper());
                for (const auto& elem : it.value()) {
                    md += QString("- **[%1]** at pixel(%2, %3) → MCP(%4, %5)\n")
                        .arg(elem.text)
                        .arg(elem.pixelX)
                        .arg(elem.pixelY)
                        .arg(elem.mcpX)
                        .arg(elem.mcpY);
                }
                md += "\n";
            }
        }

        // Summary for quick reference
        md += "## Quick Reference\n\n";
        md += "Use **MCP coordinates** (0-4096 range) with `mouse_click` tool:\n\n";
        md += "```json\n";
        md += "{\n";
        md += "  \"name\": \"mouse_click\",\n";
        md += "  \"arguments\": {\n";
        md += "    \"x\": <MCP_X>,\n";
        md += "    \"y\": <MCP_Y>\n";
        md += "  }\n";
        md += "}\n";
        md += "```\n\n";

        if (!analysis.uiElements.isEmpty()) {
            md += "### Clickable Elements\n\n";
            md += "| Element | Type | Pixel (x, y) | MCP (x, y) |\n";
            md += "|---------|------|--------------|------------|\n";
            for (const auto& elem : analysis.uiElements) {
                if (elem.type == "button" || elem.type == "menu" || elem.type == "link") {
                    md += QString("| %1 | %2 | (%3, %4) | (%5, %6) |\n")
                        .arg(elem.text)
                        .arg(elem.type.toUpper())
                        .arg(elem.pixelX)
                        .arg(elem.pixelY)
                        .arg(elem.mcpX)
                        .arg(elem.mcpY);
                }
            }
        }
    }

    return md;
}

void ScreenAnalyzer::convertToMCPCoordinates(int pixelX, int pixelY,
                                             int screenWidth, int screenHeight,
                                             int& mcpX, int& mcpY)
{
    // Convert pixel coordinates to MCP range (0-4096)
    // Formula: MCP = pixel / screen_size * 4096
    if (screenWidth > 0 && screenHeight > 0) {
        mcpX = static_cast<int>((static_cast<float>(pixelX) / screenWidth) * 4096.0f);
        mcpY = static_cast<int>((static_cast<float>(pixelY) / screenHeight) * 4096.0f);

        // Clamp to valid range
        mcpX = qBound(0, mcpX, 4096);
        mcpY = qBound(0, mcpY, 4096);
    } else {
        mcpX = 0;
        mcpY = 0;
    }
}

#ifdef HAVE_OPENCV

cv::Mat ScreenAnalyzer::QImageToMat(const QImage& image)
{
    if (image.isNull()) {
        return cv::Mat();
    }

    QImage converted = image.convertToFormat(QImage::Format_RGB32);
    cv::Mat mat(converted.height(), converted.width(), CV_8UC4,
                const_cast<uchar*>(converted.constBits()), converted.bytesPerLine());

    // Convert RGBA to BGR for OpenCV processing
    cv::Mat bgr;
    cv::cvtColor(mat, bgr, cv::COLOR_RGBA2BGR);
    return bgr;
}

QList<UIElement> ScreenAnalyzer::detectButtonsVisually(const QImage& frame,
                                                        const QList<TextElement>& textElements)
{
    QList<UIElement> buttons;

    if (frame.isNull()) {
        return buttons;
    }

    cv::Mat image = QImageToMat(frame);
    if (image.empty()) {
        return buttons;
    }

    cv::Mat gray, blurred, edges;

    // Convert to grayscale
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);

    // Apply Gaussian blur to reduce noise
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1.5);

    // Detect edges using Canny
    cv::Canny(blurred, edges, 50, 150);

    // Find contours
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(edges, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);

    int screenWidth = frame.width();
    int screenHeight = frame.height();

    for (const auto& contour : contours) {
        // Get bounding rectangle
        cv::Rect rect = cv::boundingRect(contour);

        // Filter by size - buttons should be reasonably sized
        int minWidth = 40;
        int minHeight = 20;
        int maxWidth = screenWidth / 3;
        int maxHeight = screenHeight / 4;

        if (rect.width < minWidth || rect.height < minHeight ||
            rect.width > maxWidth || rect.height > maxHeight) {
            continue;
        }

        // Check aspect ratio - buttons are typically wider than tall
        float aspectRatio = static_cast<float>(rect.width) / rect.height;
        if (aspectRatio < 1.5f || aspectRatio > 10.0f) {
            continue;
        }

        // Check if contour is roughly rectangular (solidity)
        double area = cv::contourArea(contour);
        double rectArea = static_cast<double>(rect.width * rect.height);
        if (rectArea == 0) continue;

        double solidity = area / rectArea;
        // Buttons should have high solidity (mostly filled rectangle)
        if (solidity < 0.4) {
            continue;
        }

        // Check if this region contains text
        bool containsText = false;
        QString textContent;
        for (const auto& textElem : textElements) {
            QRect textRect = textElem.boundingBox;
            cv::Rect textCvRect(textRect.x(), textRect.y(), textRect.width(), textRect.height());

            // Check if text is inside or overlapping the button region
            cv::Rect intersection = rect & textCvRect;
            if (intersection.area() > 0) {
                containsText = true;
                textContent = textElem.text;
                break;
            }
        }

        // Create UIElement for this button
        UIElement btn;
        btn.type = "button";
        btn.text = containsText ? textContent : "(icon button)";
        btn.bounds = QRect(rect.x, rect.y, rect.width, rect.height);

        int centerX = rect.x + rect.width / 2;
        int centerY = rect.y + rect.height / 2;
        btn.pixelX = centerX;
        btn.pixelY = centerY;
        convertToMCPCoordinates(centerX, centerY, screenWidth, screenHeight, btn.mcpX, btn.mcpY);

        buttons.append(btn);
    }

    // Remove duplicate/overlapping detections (keep larger ones)
    QList<UIElement> filtered;
    for (const auto& btn : buttons) {
        bool duplicate = false;
        for (const auto& existing : filtered) {
            cv::Rect r1(btn.bounds.x(), btn.bounds.y(), btn.bounds.width(), btn.bounds.height());
            cv::Rect r2(existing.bounds.x(), existing.bounds.y(), existing.bounds.width(), existing.bounds.height());
            cv::Rect intersection = r1 & r2;

            // If overlap is more than 50% of smaller rectangle, consider it duplicate
            int minArea = std::min(r1.area(), r2.area());
            if (minArea > 0 && static_cast<double>(intersection.area()) / minArea > 0.5) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            filtered.append(btn);
        }
    }

    return filtered;
}

#ifdef HAVE_OPENCV
QImage ScreenAnalyzer::preprocessForTerminal(const QImage& frame)
{
    // Convert QImage to cv::Mat
    cv::Mat mat = QImageToMat(frame);
    if (mat.empty()) {
        qCWarning(log_screen_analyzer) << "Failed to convert frame to Mat for preprocessing";
        return frame;
    }

    // Step 1: Convert to grayscale
    cv::Mat gray;
    if (mat.channels() == 3) {
        cv::cvtColor(mat, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = mat;
    }

    // Step 2: Upscale 2x if image is small (improves OCR for small terminal text)
    // Only upscale if the text would be too small for Tesseract (< 15px height)
    // Terminal text is typically ~16-20px in a 1080p terminal, so only upscale
    // if the image is significantly smaller than 1080p.
    cv::Mat scaled;
    if (gray.rows < 600) {
        cv::resize(gray, scaled, cv::Size(), 2.0, 2.0, cv::INTER_CUBIC);
        qCDebug(log_screen_analyzer) << "Upscaled terminal image 2x for better OCR";
    } else {
        scaled = gray;
    }

    // Step 3: Auto-invert if the image is predominantly dark (typical for terminals
    // with dark themes). Tesseract works best with dark text on light backgrounds.
    // Calculate mean brightness — if below threshold, invert the image.
    cv::Scalar meanBrightness = cv::mean(scaled);
    if (meanBrightness[0] < 128) {
        cv::bitwise_not(scaled, scaled);
        qCDebug(log_screen_analyzer) << "Inverted dark terminal image (mean brightness:"
                                      << meanBrightness[0] << ")";
    }

    // Step 4: Optional slight sharpening to enhance text edges
    // Use an unsharp mask: sharpened = original + amount * (original - blurred)
    cv::Mat blurred, sharpened;
    cv::GaussianBlur(scaled, blurred, cv::Size(0, 0), 1.0);
    cv::addWeighted(scaled, 1.5, blurred, -0.5, 0, sharpened);

    // Terminal screenshots already have high contrast with uniform lighting.
    // Adaptive thresholding (designed for documents with uneven lighting) actually
    // destroys terminal text quality by creating artifacts. Skip it entirely.
    // Tesseract works best with the original grayscale + slight sharpening.

    // Convert back to QImage (grayscale, not binary)
    QImage result(sharpened.cols, sharpened.rows, QImage::Format_Grayscale8);
    for (int y = 0; y < sharpened.rows; ++y) {
        memcpy(result.scanLine(y), sharpened.ptr(y), sharpened.cols);
    }

    qCDebug(log_screen_analyzer) << "Terminal preprocessing complete:"
                                 << "original" << frame.width() << "x" << frame.height()
                                 << "-> processed" << result.width() << "x" << result.height();

    return result;
}
#endif

#endif // HAVE_OPENCV

QString ScreenAnalyzer::extractTerminalText(const QImage& frame)
{
    QString terminalText;

#ifdef HAVE_TESSERACT
    if (!m_tesseract) {
        return "# Error\n\nTesseract not initialized\n";
    }

    // Preprocess image for terminal OCR if OpenCV is available
    QImage processedFrame = frame;
#ifdef HAVE_OPENCV
    if (m_opencvAvailable) {
        processedFrame = preprocessForTerminal(frame);
    }
#endif

    // Convert to RGB32 format for Tesseract
    QImage converted = processedFrame.convertToFormat(QImage::Format_RGB32);

    // Configure Tesseract for terminal output
    // PSM_SINGLE_BLOCK assumes the image is one uniform block of text and
    // reads it in normal reading order (top-to-bottom, left-to-right). This
    // preserves column alignment for output like `df -h` or `ls -l`.
    //
    // PSM_SPARSE_TEXT (previously used here) finds text anywhere in the image
    // without assuming a layout — that's useful for scattered labels on a
    // photo, but for terminal output it outputs columns out of order
    // (e.g. "Filesystem" appears last instead of first).
    m_tesseract->SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);

    // Set variables optimized for terminal/command output
    m_tesseract->SetVariable("preserve_interword_spaces", "1");
    m_tesseract->SetVariable("textord_heavy_line_nr", "0");
    m_tesseract->SetVariable("language_model_ngram_on", "0");

    // Set the image data
    m_tesseract->SetImage(
        converted.constBits(),
        converted.width(),
        converted.height(),
        4,  // bytes per pixel (RGB32 = 4 bytes)
        converted.bytesPerLine()
    );

    // Recognize and get full text
    m_tesseract->Recognize(nullptr);
    char* outText = m_tesseract->GetUTF8Text();

    if (outText) {
        terminalText = QString::fromUtf8(outText);
        delete[] outText;
    }

    // Restore default PSM mode for subsequent general OCR calls
    m_tesseract->SetPageSegMode(tesseract::PSM_AUTO);

    qCInfo(log_screen_analyzer) << "Terminal OCR extracted" << terminalText.length() << "characters";
#else
    Q_UNUSED(frame)
    terminalText = "# Error\n\nTesseract OCR not available\n";
#endif

    return terminalText;
}

// ============================================================================
// Non-OpenCV-dependent helpers (also available when OpenCV is absent)
// ============================================================================

QString ScreenAnalyzer::generateTerminalMarkdown(const QString& terminalText, int screenWidth, int screenHeight,
                                                  const QRect* changedRect)
{
    QString md;

    if (changedRect && !changedRect->isNull()) {
        md += QString("# Terminal Output — changed region (%1x%2) at (%3,%4)-(%5,%6)\n\n")
            .arg(changedRect->width()).arg(changedRect->height())
            .arg(changedRect->x()).arg(changedRect->y())
            .arg(changedRect->right()).arg(changedRect->bottom());
        md += "_Only the changed part of the screen is shown below._\n\n";
    } else {
        md += QString("# Terminal Output (%1x%2)\n\n").arg(screenWidth).arg(screenHeight);
    }

    if (terminalText.trimmed().isEmpty()) {
        md += "_No text detected in terminal._\n";
    } else {
        md += "```\n";
        md += terminalText;
        if (!terminalText.endsWith('\n')) {
            md += '\n';
        }
        md += "```\n";
    }

    return md;
}

bool ScreenAnalyzer::detectChangedRegion(const QImage& currentFrame, QRect& changedRect, float& changeRatio)
{
    changedRect = QRect();
    changeRatio = 0.0f;

    if (m_previousFrame.isNull() || m_previousFrame.size() != currentFrame.size()) {
        // No previous frame to compare, or size mismatch — can't detect changes.
        // The size check matters because we store a downscaled copy; if the screen
        // resolution changed between calls, the diff would be meaningless.
        return false;
    }

#ifdef HAVE_OPENCV
    // Convert both frames to grayscale Mats for comparison.
    // We use the stored downscale copy vs. a fresh downscale of the current frame
    // so both are at the same resolution for absdiff.
    cv::Mat prevMat = QImageToMat(m_previousFrame);
    cv::Mat currFullMat = QImageToMat(currentFrame);

    if (prevMat.empty() || currFullMat.empty()) {
        return false;
    }

    // Downscale current frame to match the stored previous frame size
    cv::Mat currMat;
    if (currFullMat.cols != prevMat.cols || currFullMat.rows != prevMat.rows) {
        cv::resize(currFullMat, currMat, prevMat.size(), 0, 0, cv::INTER_AREA);
    } else {
        currMat = currFullMat;
    }

    cv::Mat prevGray, currGray;
    cv::cvtColor(prevMat, prevGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(currMat, currGray, cv::COLOR_BGR2GRAY);

    // Compute absolute difference between frames
    cv::Mat diff;
    cv::absdiff(prevGray, currGray, diff);

    // Threshold to find pixels that changed significantly.
    // 20/255 filters out compression artifacts (H.264 introduces small per-pixel
    // changes even on static frames) and ambient noise.
    cv::Mat binary;
    cv::threshold(diff, binary, 20, 255, cv::THRESH_BINARY);

    // Morphological close to merge nearby changed pixels into connected regions.
    // Without this, a single line of new terminal output would be detected as
    // dozens of tiny fragments — each character a separate contour.
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

    // Find contours of changed regions
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) {
        return true;  // Have previous, but no changes detected
    }

    // Compute bounding box that contains all changed regions.
    // Work in the original (full-resolution) coordinate space so the rect can
    // be used to crop the full-res frame for OCR.
    int scale = 2;  // We store at half resolution, so multiply back up
    int minX = currentFrame.width(), minY = currentFrame.height();
    int maxX = 0, maxY = 0;
    int totalChangedPixels = 0;

    for (const auto& contour : contours) {
        cv::Rect r = cv::boundingRect(contour);

        // Filter out tiny noise contours
        if (r.width < 5 || r.height < 5) continue;

        // Scale up to full resolution
        int fx = r.x * scale;
        int fy = r.y * scale;
        int fw = r.width * scale;
        int fh = r.height * scale;

        // Filter out very large contours that span most of the screen
        // (full-screen refresh, cursor blink covering everything, etc.)
        if (fw > currentFrame.width() * 0.9 && fh > currentFrame.height() * 0.9) {
            // Full-screen change — fall back to full-frame OCR
            return true;
        }

        minX = std::min(minX, fx);
        minY = std::min(minY, fy);
        maxX = std::max(maxX, fx + fw);
        maxY = std::max(maxY, fy + fh);
        totalChangedPixels += fw * fh;
    }

    if (maxX <= minX || maxY <= minY) {
        return true;  // Have previous, but changes were too small to matter
    }

    changedRect = QRect(minX, minY, maxX - minX, maxY - minY);
    int screenPixels = currentFrame.width() * currentFrame.height();
    changeRatio = (screenPixels > 0) ? static_cast<float>(totalChangedPixels) / screenPixels : 0.0f;

    qCDebug(log_screen_analyzer) << "Changed region:" << changedRect
                                 << "ratio:" << changeRatio
                                 << "contours:" << contours.size();
    return true;
#else
    Q_UNUSED(currentFrame)
    return false;
#endif
}

void ScreenAnalyzer::updatePreviousFrame(const QImage& frame)
{
    // Store a downscaled copy to save memory. We only need it for diff comparison,
    // not for high-res OCR. Half resolution is enough for change detection and
    // keeps memory usage bounded (a 4K frame = ~8MB RGB vs ~2MB at half-res).
    if (!frame.isNull()) {
        m_previousFrame = frame.scaled(qMax(1, frame.width() / 2), qMax(1, frame.height() / 2),
                                        Qt::IgnoreAspectRatio, Qt::FastTransformation);
    }
}

void ScreenAnalyzer::clearPreviousFrame()
{
    m_previousFrame = QImage();
}

// ============================================================================
// Cursor Blink Detection (Temporal Differencing)
// ============================================================================
//
// This detects whether a terminal is waiting for user input by looking for a
// blinking cursor. The key insight: in an idle terminal, the ONLY thing that
// changes between consecutive frames is the cursor blinking on and off.
//
// Algorithm:
//   1. The caller captures N frames at ~500ms intervals and passes them here.
//   2. We compute pixel diffs (cv::absdiff) between each consecutive pair.
//   3. For each diff, we find small changed regions (cursor-sized: < 2% of screen).
//   4. We check whether the same screen position changed in multiple consecutive
//      diffs. A blinking cursor will toggle at a fixed position across frames.
//      New terminal output will change at different positions each time.
//   5. If 2+ consecutive diffs show changes at the same small position → cursor.
//
// Why this works:
//   - A cursor blink is a periodic toggle at a fixed location (~0.5-1Hz).
//   - Terminal output flows downward, changing different lines each time.
//   - H.264 compression noise is filtered by the threshold (20/255).
//
// Limitations:
//   - Requires the terminal to be mostly idle (no scrolling output).
//   - Cannot detect a static (non-blinking) cursor — needs temporal change.
//   - Cursor must be visually distinct (not the same color as the background).
//   - Very small or very large cursors may be missed (filtered by size heuristics).
//
// ============================================================================

CursorDetectionResult ScreenAnalyzer::detectCursorFromFrames(const QList<QImage>& frames)
{
    CursorDetectionResult result;
    result.detected = false;
    result.position = QRect();
    result.mcpX = 0;
    result.mcpY = 0;
    result.confidence = 0.0f;
    result.status = "unknown";
    result.description = "Insufficient data for cursor detection.";
    result.cursorBlinkDetected = false;
    result.screenStable = false;
    result.totalChangeRatio = 0.0f;
    result.promptDetected = false;

    // Need at least 3 frames to detect a blink pattern
    // (2 diffs, and the cursor must toggle in at least 2 of them)
    if (frames.size() < 3) {
        result.description = QString("Need at least 3 frames, got %1").arg(frames.size());
        qCWarning(log_screen_analyzer) << "Cursor detection: insufficient frames:" << frames.size();
        return result;
    }

    // Verify all frames have the same size
    int width = frames[0].width();
    int height = frames[0].height();
    for (int i = 1; i < frames.size(); ++i) {
        if (frames[i].size() != frames[0].size()) {
            result.description = "Frames have inconsistent sizes.";
            qCWarning(log_screen_analyzer) << "Cursor detection: frame size mismatch at index" << i;
            return result;
        }
    }

    qCInfo(log_screen_analyzer) << "Cursor detection: analyzing" << frames.size()
                                << "frames of size" << width << "x" << height;

    // Step 1: Compute diffs between consecutive frames and find small changes
    QList<QList<ChangeRegion>> diffsChanges;  // changes[i] = small changes in diff(i, i+1)
    int numDiffs = frames.size() - 1;

    for (int i = 0; i < numDiffs; ++i) {
        QList<ChangeRegion> changes = findSmallChanges(frames[i], frames[i + 1]);
        diffsChanges.append(changes);

        qCDebug(log_screen_analyzer) << "  diff" << i << "→" << (i + 1)
                                     << ": found" << changes.size() << "small change regions";
    }

    // Step 1b: Compute screen stability.
    //
    // Measure the total change ratio across all diffs. This tells us how much of
    // the screen is changing between frames, regardless of where.
    //
    // - Very low ratio (< 0.1%): screen is almost completely static. Either the
    //   cursor isn't blinking, or changes are below the noise threshold.
    // - Low ratio (0.1% - 0.5%): only cursor-level changes. The screen is stable —
    //   a terminal at a prompt with a blinking (or non-blinking) cursor.
    // - Medium ratio (0.5% - 5%): some content changing. Could be a slow command
    //   outputting one line at a time, or a fast command nearing completion.
    // - High ratio (> 5%): active output. Terminal is clearly producing content.
    //
    // We use findSmallChanges with a generous maxRatio (10%) to capture ALL changes,
    // not just cursor-sized ones. This gives us the true total change ratio.

    float totalChangeRatio = 0.0f;
    {
        // Use a higher maxRatio to capture all changes, not just cursor-sized ones
        for (int i = 0; i < numDiffs; ++i) {
            QList<ChangeRegion> allChanges = findSmallChanges(frames[i], frames[i + 1], 0.10f);
            float diffRatio = 0.0f;
            for (const ChangeRegion& c : allChanges) {
                diffRatio += c.ratio;
            }
            totalChangeRatio = qMax(totalChangeRatio, diffRatio);
        }
    }

    // Screen is "stable" if total change is below the level of a single line of output.
    // A cursor blink changes ~0.01-0.03% of a 1080p screen.
    // A single line of terminal output changes ~0.1-0.5% of the screen.
    // We use 0.1% as the threshold — above cursor noise, below one line of text.
    bool screenStable = (totalChangeRatio < 0.001f);

    result.screenStable = screenStable;
    result.totalChangeRatio = totalChangeRatio;

    qCInfo(log_screen_analyzer) << "Screen stability: totalChangeRatio =" << totalChangeRatio
                                << "stable:" << screenStable;

    // Step 1c: Detect shell prompt on the last line.
    //
    // Use OCR to read the last few lines of the terminal and check for common
    // shell prompt patterns. This is a strong signal that the terminal is at a
    // command prompt and waiting for input.

    QString promptText;
    bool promptDetected = detectShellPrompt(frames.last(), promptText);
    result.promptDetected = promptDetected;
    result.promptText = promptText;

    qCInfo(log_screen_analyzer) << "Prompt detection:" << (promptDetected ? "found" : "none")
                                << (promptDetected ? ("'" + promptText + "'") : "");

    // Step 2: Look for a cursor blink pattern.
    //
    // We're looking for a small region that appears in multiple consecutive diffs
    // at approximately the same position. This is the signature of a blinking cursor:
    //
    //   Frame 0: cursor ON  ─┐
    //   Frame 1: cursor OFF  ├─ diff 0→1 shows change at cursor position
    //   Frame 2: cursor ON   ├─ diff 1→2 shows change at SAME cursor position
    //   Frame 3: cursor OFF ─┘  diff 2→3 shows change at SAME cursor position
    //
    // vs. terminal output (different positions each time):
    //
    //   Frame 0: line 10 ─┐
    //   Frame 1: line 11  ├─ diff 0→1 shows change at line 11
    //   Frame 2: line 12  ├─ diff 1→2 shows change at line 12 (DIFFERENT position)
    //   Frame 3: line 13 ─┘  diff 2→3 shows change at line 13 (DIFFERENT position)

    // Find the best candidate: a position that appears in the most consecutive diffs
    int bestMatchCount = 0;
    QRect bestPosition;
    int bestDiffStart = -1;  // First diff index where this position appears

    for (int i = 0; i < numDiffs; ++i) {
        for (const ChangeRegion& regionA : diffsChanges[i]) {
            int matchCount = 1;  // This region counts as the first match
            QRect accumulatedRect = regionA.rect;

            // Check how many subsequent diffs have a change at the same position
            for (int j = i + 1; j < numDiffs; ++j) {
                bool foundMatch = false;
                for (const ChangeRegion& regionB : diffsChanges[j]) {
                    if (isSamePosition(regionA, regionB)) {
                        matchCount++;
                        // Expand the accumulated rect to include all matches
                        accumulatedRect = accumulatedRect.united(regionB.rect);
                        foundMatch = true;
                        break;
                    }
                }
                if (!foundMatch) {
                    break;  // Stop at the first diff without a match
                }
            }

            if (matchCount > bestMatchCount) {
                bestMatchCount = matchCount;
                bestPosition = accumulatedRect;
                bestDiffStart = i;
            }
        }
    }

    qCInfo(log_screen_analyzer) << "Cursor detection: best match count =" << bestMatchCount
                                << "at position" << bestPosition
                                << "starting from diff" << bestDiffStart;

    // Step 3: Combine all signals into a final verdict.
    //
    // We have three independent signals:
    //   Signal 1: Cursor blink — a small region toggling at a fixed position
    //   Signal 2: Screen stability — total change ratio below threshold
    //   Signal 3: Shell prompt — OCR detected a prompt pattern on the last line
    //
    // Decision matrix:
    //   Screen NOT stable (changes > 0.5%) → "outputting" regardless of other signals
    //   Screen stable + cursor blink + prompt → "idle" (highest confidence)
    //   Screen stable + cursor blink         → "idle" (high confidence)
    //   Screen stable + prompt               → "likely_idle" (moderate confidence)
    //   Screen stable only                   → "likely_idle" (lower confidence)
    //   No signals at all                    → "unknown"

    // First: if the screen is NOT stable, it's outputting — done.
    if (!screenStable) {
        // Double-check: if we found a cursor blink, verify there are no other changes
        if (bestMatchCount >= 2) {
            int changesAtOtherPositions = 0;
            QPointF cursorCentroid(bestPosition.center().x(), bestPosition.center().y());
            for (int i = 0; i < numDiffs; ++i) {
                for (const ChangeRegion& region : diffsChanges[i]) {
                    double dx = region.centroid.x() - cursorCentroid.x();
                    double dy = region.centroid.y() - cursorCentroid.y();
                    double dist = std::sqrt(dx * dx + dy * dy);
                    QRect expandedCursor = bestPosition.adjusted(-10, -10, 10, 10);
                    bool atCursorPos = (dist <= 30.0) && expandedCursor.intersects(region.rect);
                    if (!atCursorPos) changesAtOtherPositions++;
                }
            }
            if (changesAtOtherPositions > 0) {
                result.detected = false;
                result.status = "outputting";
                result.description = QString("Screen is changing (%1% change ratio) with "
                    "%2 change(s) at positions other than the cursor. Terminal is producing output.")
                    .arg(totalChangeRatio * 100, 0, 'f', 2).arg(changesAtOtherPositions);
                return result;
            }
        }

        result.detected = false;
        result.status = "outputting";
        result.description = QString("Screen change ratio is %1% — above the 0.1% stability "
            "threshold. Terminal is producing output or transitioning.").arg(totalChangeRatio * 100, 0, 'f', 2);
        return result;
    }

    // Screen IS stable. Now combine cursor blink and prompt signals.
    bool cursorBlinkValid = false;
    float cursorConfidence = 0.0f;
    QRect cursorPos;

    if (bestMatchCount >= 2) {
        int rw = bestPosition.width();
        int rh = bestPosition.height();

        // Validate cursor size
        if (rw >= 4 && rh >= 4 && rw <= 80 && rh <= 50) {
            cursorBlinkValid = true;

            // Compute cursor confidence
            if (bestMatchCount >= numDiffs) cursorConfidence = 0.95f;
            else if (bestMatchCount == 3) cursorConfidence = 0.9f;
            else cursorConfidence = 0.65f;

            // Bonus for cursor proportions
            float aspectRatio = static_cast<float>(rw) / qMax(1, rh);
            if (aspectRatio >= 0.3f && aspectRatio <= 1.5f) cursorConfidence += 0.05f;
            else if (aspectRatio > 2.0f && rh < 8) cursorConfidence += 0.05f;

            cursorPos = bestPosition;
        }
    }

    result.cursorBlinkDetected = cursorBlinkValid;

    // Combine signals
    if (cursorBlinkValid && promptDetected) {
        // Strongest: cursor blink + screen stable + prompt detected
        float confidence = qMin(1.0f, cursorConfidence + 0.05f);
        int centerX = cursorPos.x() + cursorPos.width() / 2;
        int centerY = cursorPos.y() + cursorPos.height() / 2;
        int mcpX, mcpY;
        convertToMCPCoordinates(centerX, centerY, width, height, mcpX, mcpY);

        result.detected = true;
        result.position = cursorPos;
        result.mcpX = mcpX;
        result.mcpY = mcpY;
        result.confidence = confidence;
        result.status = "idle";
        result.description = QString("Terminal is idle. Blinking cursor at pixel(%1, %2) MCP(%3, %4), "
            "screen stable (%5% change), shell prompt detected (\"%6\").")
            .arg(centerX).arg(centerY).arg(mcpX).arg(mcpY)
            .arg(totalChangeRatio * 100, 0, 'f', 2).arg(promptText);
    } else if (cursorBlinkValid) {
        // Cursor blink + stable screen, no prompt detected
        int centerX = cursorPos.x() + cursorPos.width() / 2;
        int centerY = cursorPos.y() + cursorPos.height() / 2;
        int mcpX, mcpY;
        convertToMCPCoordinates(centerX, centerY, width, height, mcpX, mcpY);

        result.detected = true;
        result.position = cursorPos;
        result.mcpX = mcpX;
        result.mcpY = mcpY;
        result.confidence = cursorConfidence;
        result.status = "idle";
        result.description = QString("Terminal is idle. Blinking cursor at pixel(%1, %2) MCP(%3, %4), "
            "screen stable (%5% change).")
            .arg(centerX).arg(centerY).arg(mcpX).arg(mcpY)
            .arg(totalChangeRatio * 100, 0, 'f', 2);
    } else if (promptDetected) {
        // No cursor blink, but screen is stable and prompt detected
        result.detected = true;
        result.confidence = 0.75f;
        result.status = "likely_idle";
        result.description = QString("Terminal likely idle. Screen is stable (%1% change) and shell "
            "prompt detected (\"%2\"). No cursor blink detected (cursor may be non-blinking or "
            "have a slow blink rate).")
            .arg(totalChangeRatio * 100, 0, 'f', 2).arg(promptText);
    } else {
        // Screen stable but no cursor blink and no prompt
        result.detected = true;
        result.confidence = 0.5f;
        result.status = "likely_idle";
        result.description = QString("Screen is stable (%1% change) with no significant changes. "
            "Terminal is likely idle but cursor blink was not detected and no shell prompt was found. "
            "Cursor may be non-blinking or terminal may not be at a standard shell prompt.")
            .arg(totalChangeRatio * 100, 0, 'f', 2);
    }

    qCInfo(log_screen_analyzer) << "Terminal detection result:" << result.status
                                << "confidence:" << result.confidence
                                << "description:" << result.description;
    return result;
}

QList<ChangeRegion> ScreenAnalyzer::findSmallChanges(const QImage& frameA, const QImage& frameB,
                                                      float maxRatio)
{
    QList<ChangeRegion> results;

#ifdef HAVE_OPENCV
    cv::Mat matA = QImageToMat(frameA);
    cv::Mat matB = QImageToMat(frameB);

    if (matA.empty() || matB.empty()) {
        return results;
    }

    // Convert to grayscale for comparison
    cv::Mat grayA, grayB;
    cv::cvtColor(matA, grayA, cv::COLOR_BGR2GRAY);
    cv::cvtColor(matB, grayB, cv::COLOR_BGR2GRAY);

    // Compute absolute difference
    cv::Mat diff;
    cv::absdiff(grayA, grayB, diff);

    // Threshold to find significant changes.
    // 25/255 is slightly higher than the 20/255 used in detectChangedRegion
    // to reduce noise. Cursor pixels typically change by 100+ (dark→light or vice versa).
    cv::Mat binary;
    cv::threshold(diff, binary, 25, 255, cv::THRESH_BINARY);

    // Morphological close to merge nearby changed pixels into connected regions.
    // Use a small kernel — cursor changes are small, we don't want to merge
    // unrelated regions.
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::morphologyEx(binary, binary, cv::MORPH_CLOSE, kernel);

    // Find contours of changed regions
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    int screenPixels = frameA.width() * frameA.height();
    float maxAllowedPixels = screenPixels * maxRatio;

    for (const auto& contour : contours) {
        cv::Rect r = cv::boundingRect(contour);

        // Scale up if we're working with downscaled frames (we're not in this case,
        // but keep the pattern consistent with detectChangedRegion)

        // Filter out tiny noise
        if (r.width < 3 || r.height < 3) continue;

        // Filter out regions that are too large (not cursor-sized)
        int changedPixelCount = r.width * r.height;
        if (changedPixelCount > maxAllowedPixels) continue;

        float ratio = static_cast<float>(changedPixelCount) / screenPixels;

        // Compute centroid using image moments for accuracy
        cv::Moments moments = cv::moments(contour);
        double cx = r.x + r.width / 2.0;
        double cy = r.y + r.height / 2.0;
        if (moments.m00 > 0) {
            cx = moments.m10 / moments.m00;
            cy = moments.m01 / moments.m00;
        }

        ChangeRegion region;
        region.rect = QRect(r.x, r.y, r.width, r.height);
        region.pixelCount = changedPixelCount;
        region.ratio = ratio;
        region.centroid = QPointF(cx, cy);

        results.append(region);
    }

    qCDebug(log_screen_analyzer) << "findSmallChanges: found" << results.size()
                                 << "regions (contours:" << contours.size()
                                 << "max ratio:" << maxRatio << ")";
#else
    Q_UNUSED(frameA)
    Q_UNUSED(frameB)
    Q_UNUSED(maxRatio)
    qCWarning(log_screen_analyzer) << "findSmallChanges: OpenCV not available";
#endif

    return results;
}

bool ScreenAnalyzer::isSamePosition(const ChangeRegion& a, const ChangeRegion& b, float tolerance)
{
    // Two change regions are at the "same position" if their centroids are
    // within `tolerance` pixels of each other AND their bounding boxes overlap
    // or are close to each other.
    //
    // The centroid check handles the case where the cursor moved slightly
    // (e.g., due to H.264 compression shifting edges by 1-2 pixels).
    // The bounding box overlap check ensures we're looking at the same region.

    // Check centroid distance
    double dx = a.centroid.x() - b.centroid.x();
    double dy = a.centroid.y() - b.centroid.y();
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance > tolerance) {
        return false;
    }

    // Check if bounding boxes are close (within a small margin)
    // This handles cases where the cursor bounding box shifts slightly
    int margin = 10;  // pixels
    QRect expandedA = a.rect.adjusted(-margin, -margin, margin, margin);
    QRect expandedB = b.rect.adjusted(-margin, -margin, margin, margin);

    return expandedA.intersects(expandedB);
}

// ============================================================================
// Shell Prompt Detection (OCR)
// ============================================================================
//
// Detects common shell prompt patterns on the last line of a terminal screen.
// This is a strong signal that the terminal is at a command prompt and waiting
// for user input.
//
// Supported prompt patterns:
//   - Bourne shell / bash:   "$ ", "# " (root), "user@host:~$ "
//   - C shell / tcsh:        "% ", "host% "
//   - PowerShell:            "PS C:\> ", "PS> "
//   - Python REPL:           ">>> ", "... "
//   - Node.js REPL:          "> "
//   - MySQL:                 "mysql> ", "    -> "
//   - Generic:               "> ", "? "
//
// The method OCRs the bottom 15% of the screen (where the prompt line lives)
// and uses regex to match known prompt patterns.
//
// ============================================================================

bool ScreenAnalyzer::detectShellPrompt(const QImage& frame, QString& promptText)
{
    promptText.clear();

#ifdef HAVE_TESSERACT
    if (!m_tesseract) {
        return false;
    }

    // Crop the bottom 15% of the screen — that's where the prompt line lives.
    // Also take the full width since the prompt can be anywhere horizontally.
    int cropHeight = qMax(30, frame.height() / 7);  // ~14% of screen height, min 30px
    int cropY = frame.height() - cropHeight;
    QImage bottomStrip = frame.copy(0, cropY, frame.width(), cropHeight);

    if (bottomStrip.isNull()) {
        return false;
    }

    // Preprocess for terminal OCR (reuse existing method if OpenCV available)
    QImage processed = bottomStrip;
#ifdef HAVE_OPENCV
    if (m_opencvAvailable) {
        processed = preprocessForTerminal(bottomStrip);
    }
#endif

    // Convert for Tesseract
    QImage converted = processed.convertToFormat(QImage::Format_RGB32);

    // Configure for single-line text
    m_tesseract->SetPageSegMode(tesseract::PSM_SINGLE_LINE);
    m_tesseract->SetVariable("preserve_interword_spaces", "1");

    m_tesseract->SetImage(
        converted.constBits(),
        converted.width(),
        converted.height(),
        4,
        converted.bytesPerLine()
    );

    m_tesseract->Recognize(nullptr);
    char* outText = m_tesseract->GetUTF8Text();

    // Restore default PSM
    m_tesseract->SetPageSegMode(tesseract::PSM_AUTO);

    if (!outText) {
        return false;
    }

    QString text = QString::fromUtf8(outText).trimmed();
    delete[] outText;

    if (text.isEmpty()) {
        return false;
    }

    qCDebug(log_screen_analyzer) << "Prompt OCR bottom strip:" << text;

    // Match known prompt patterns. We check for these at the END of the text
    // (the prompt is the last thing on the line, followed by the cursor).
    //
    // Patterns (checked longest-first to avoid partial matches):
    static const QList<QRegularExpression> promptPatterns = {
        // Bash / sh prompts (most common)
        QRegularExpression(R"(\$\s*$)"),           // "$ " at end
        QRegularExpression(R"(#\s*$)"),            // "# " at end (root)
        QRegularExpression(R"(:\S*\$\s*$)"),       // "host:~$ " or ":dir$ "
        QRegularExpression(R"(:\S*#\s*$)"),        // "host:~# " (root)
        QRegularExpression(R"(@\S+:\S*\$\s*$)"),   // "user@host:~$ "
        QRegularExpression(R"(@\S+:\S*#\s*$)"),    // "user@host:~# "

        // C shell / tcsh
        QRegularExpression(R"(%\s*$)"),            // "% " at end
        QRegularExpression(R"(\S+%\s*$)"),         // "host% "

        // PowerShell
        QRegularExpression(R"(PS\s+\S+>\s*$)"),    // "PS C:\> "
        QRegularExpression(R"(PS>\s*$)"),           // "PS> "

        // Python / Node / generic REPL
        QRegularExpression(R"(>>>\s*$)"),           // ">>> "
        QRegularExpression(R"(\.\.\.\s*$)"),        // "... "
        QRegularExpression(R"(>\s*$)"),             // "> "

        // MySQL / database
        QRegularExpression(R"(mysql>\s*$)"),
        QRegularExpression(R"(->\s*$)"),

        // vi/vim (shows ":" at bottom)
        QRegularExpression(R"(:\s*$)"),

        // Fish shell
        QRegularExpression(R"(\S+>\s*$)"),         // "dir> "
    };

    for (const auto& pattern : promptPatterns) {
        if (pattern.isValid()) {
            auto match = pattern.match(text);
            if (match.hasMatch()) {
                promptText = match.captured(0).trimmed();
                qCInfo(log_screen_analyzer) << "Shell prompt detected:" << promptText
                                            << "from OCR text:" << text;
                return true;
            }
        }
    }

    qCDebug(log_screen_analyzer) << "No prompt pattern found in OCR text:" << text;
    return false;

#else
    Q_UNUSED(frame)
    qCWarning(log_screen_analyzer) << "detectShellPrompt: Tesseract not available";
    return false;
#endif
}
