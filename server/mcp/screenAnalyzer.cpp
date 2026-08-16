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

#ifdef HAVE_TESSERACT
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#endif

#ifdef HAVE_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
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

ScreenAnalysis ScreenAnalyzer::analyzeScreen(const QImage& frame, const QString& detailLevel)
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

    qCInfo(log_screen_analyzer) << "Analyzing screen" << result.screenWidth << "x" << result.screenHeight;

    // Extract text elements with positions
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

#endif // HAVE_OPENCV
