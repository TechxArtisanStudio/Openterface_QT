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

#ifndef VIDEOSETTINGS_H
#define VIDEOSETTINGS_H

#include <QDialog>
#include <QCameraDevice>
#include <set>

QT_BEGIN_NAMESPACE
class QCameraFormat;
class QComboBox;
class QCamera;
namespace Ui {
class VideoSettingsUi;
}
QT_END_NAMESPACE

// Custom key structure
struct VideoFormatKey {
    QSize resolution;
    int frameRate;
    QVideoFrameFormat::PixelFormat pixelFormat;

    bool operator<(const VideoFormatKey &other) const {
        if (resolution.width() != other.resolution.width())
            return resolution.width() < other.resolution.width();
        if (resolution.height() != other.resolution.height())
            return resolution.height() < other.resolution.height();
        if (frameRate != other.frameRate)
            return frameRate < other.frameRate;
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

class VideoSettings : public QDialog
{
    Q_OBJECT

public:
    explicit VideoSettings(QCamera *camera, QWidget *parent = nullptr);
    ~VideoSettings();

    void applySettings();
    void updateFormatsAndCodecs();

protected:
    void changeEvent(QEvent *e) override;

private:
    void setFpsRange(const std::set<int> &fpsRange);
    QVariant boxValue(const QComboBox *) const;
    void selectComboBoxItem(QComboBox *box, const QVariant &value);
    void populateResolutionBox(const QList<QCameraFormat> &videoFormats);
    std::map<VideoFormatKey, QCameraFormat> videoFormatMap;
    QCameraFormat getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
    Ui::VideoSettingsUi *ui;
    QCamera *camera;
    bool m_updatingFormats = false;

    QSize m_currentResolution;


private slots:
    void onFpsSliderValueChanged(int value);

};

#endif // VIDEOSETTINGS_H
