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

#ifndef VIDEOPAGE_H
#define VIDEOPAGE_H
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSize>
#include <QMap>
#include <set>
#include "fontstyle.h"
#include "host/cameramanager.h"
#include <QSettings>
#include "globalsetting.h"
#include "global.h"

QT_BEGIN_NAMESPACE
class QCameraFormat;
QT_END_NAMESPACE

// Struct to represent a video format key, used for comparing and sorting video formats
// It includes resolution, frame rate range, and pixel format
struct VideoFormatKey {
    QSize resolution;
    int minFrameRate;
    int maxFrameRate;
    QVideoFrameFormat::PixelFormat pixelFormat;

    bool operator<(const VideoFormatKey &other) const {
        if (resolution.width() != other.resolution.width())
            return resolution.width() < other.resolution.width();
        if (resolution.height() != other.resolution.height())
            return resolution.height() < other.resolution.height();
        if (minFrameRate != other.minFrameRate)
            return minFrameRate < other.minFrameRate;
        if (maxFrameRate != other.maxFrameRate)
            return maxFrameRate < other.maxFrameRate;
        return pixelFormat < other.pixelFormat;
    }
};


struct QSizeComparator {
    bool operator()(const QSize& lhs, const QSize& rhs) const {
        if (lhs.width() == rhs.width()) {
            return lhs.height() > rhs.height(); // Compare heights in descending order
        }
        return lhs.width() > rhs.width(); // Compare widths in descending order
    }
};

class VideoPage : public QWidget
{
    Q_OBJECT
public:
    explicit VideoPage(CameraManager *cameraManager, QWidget *parent = nullptr);
    void setupUI();
    void initVideoSettings();
    void applyVideoSettings();
    
signals:
    void videoSettingsChanged(int width, int height);

private:
    CameraManager *m_cameraManager;
    QSize m_currentResolution;
    bool m_updatingFormats = false;
    std::map<VideoFormatKey, QCameraFormat> videoFormatMap;

    QLabel *videoLabel;
    QLabel *resolutionsLabel;
    QComboBox *videoFormatBox;
    QLabel *framerateLabel;
    QComboBox *fpsComboBox;
    QLabel *formatLabel;
    QComboBox *pixelFormatBox;
    void populateResolutionBox(const QList<QCameraFormat> &videoFormats);
    void setFpsRange(const std::set<int> &fpsValues);
    QVariant boxValue(const QComboBox *) const;
    void updatePixelFormats();
    QCameraFormat getVideoFormat(const QSize &resolution, int desiredFrameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
};

#endif // VIDEOPAGE_H

