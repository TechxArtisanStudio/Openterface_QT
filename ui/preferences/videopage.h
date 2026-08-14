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
#include "ui/globalsetting.h"
#include "global.h"
#include "preferencepagebase.h"

QT_BEGIN_NAMESPACE
class QCameraFormat;
QT_END_NAMESPACE


struct QSizeComparator {
    bool operator()(const QSize& lhs, const QSize& rhs) const {
        if (lhs.width() == rhs.width()) {
            return lhs.height() > rhs.height(); // Compare heights in descending order
        }
        return lhs.width() > rhs.width(); // Compare widths in descending order
    }
};

class VideoPage : public PreferencePageBase
{
    Q_OBJECT
public:
    explicit VideoPage(CameraManager *cameraManager, QWidget *parent = nullptr);
    void setupUI();
    void initVideoSettings();
    void applySettings() override;
    void captureSnapshot() override;
    void revertToSnapshot() override;

signals:
    void videoSettingsChanged();
    void inputResolutionChanged(const QSize &resolution);
    void cameraSettingsApplied();
    void cameraDeviceChanged();

private slots:
    void toggleCustomResolutionInputs(bool checked);
    void onMediaBackendChanged();



private:
    CameraManager *m_cameraManager;
    QSize m_currentResolution;
    bool m_updatingFormats = false;

    QLabel *videoLabel;
    QLabel *resolutionsLabel;
    QComboBox *videoFormatBox;
    QLabel *framerateLabel;
    QComboBox *fpsComboBox;
    QLabel *formatLabel;
    QComboBox *pixelFormatBox;
    // Snapshot members
    int m_snap_videoFormatIndex;
    int m_snap_pixelFormatIndex;
    int m_snap_fpsIndex;
    QSize m_snap_resolution;
    int m_snap_hwAccelIndex;
    int m_snap_scalingQualityIndex;
    bool m_snap_antialiasing;
    bool m_snap_textAntialiasing;
    bool m_snap_smoothTransform;
    int m_snap_mediaBackendIndex;
    bool m_snap_overrideSettings;
    int m_snap_customWidth;
    int m_snap_customHeight;
    QString m_snap_gstSinkPriority;
    void populateResolutionBox(const QList<QCameraFormat> &videoFormats);
    void setFpsRange(const std::set<int> &fpsValues);
    void handleResolutionSettings();
    QVariant boxValue(const QComboBox *) const;
    void updatePixelFormats();

};

#endif // VIDEOPAGE_H
