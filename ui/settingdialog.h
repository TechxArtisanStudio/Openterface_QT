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

#ifndef SETTINGDIALOG_H
#define SETTINGDIALOG_H

#include <QDialog>
#include <QCamera>
#include <QMediaFormat>
#include <QCameraDevice>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <set>
#include <QMediaDevices>
#include <QByteArray>
#include <QMap>
#include <QCheckBox>
#include <QLineEdit>
#include <QByteArray>
#include "host/cameramanager.h"
#include "logpage.h"

QT_BEGIN_NAMESPACE
class QCameraFormat;
class QComboBox;
class QCamera;
namespace Ui {
class SettingDialog;
}
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

class SettingDialog : public QDialog
{
    Q_OBJECT

public:
    // Change the constructor to accept CameraManager instead of QCamera
    explicit SettingDialog(CameraManager *cameraManager, QWidget *parent = nullptr);
    ~SettingDialog();

signals:
    void cameraSettingsApplied();
    void serialSettingsApplied();
    void videoSettingsChanged(int width, int height);

private:
    
    const QString bigLabelFontSize = "QLabel { font-size: 14px; }";
    const QString smallLabelFontSize = "QLabel { font-size: 12px; }";
    const QString commentsFontSize = "QLabel { font-size: 10px; }";

    Ui::SettingDialog *ui;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    LogPage *logPage;
    QWidget *videoPage;
    QWidget *audioPage;
    QWidget *hardwarePage;
    QWidget *buttonWidget;


    QMap<QCheckBox *, QLineEdit *> USBCheckBoxEditMap; // map of checkboxes to line edit about VID PID etc.
    void addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit);

    CameraManager *m_cameraManager;
    QSize m_currentResolution;
    bool m_updatingFormats = false;
    std::map<VideoFormatKey, QCameraFormat> videoFormatMap;
    // QCameraFormat getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
    
    void switchWidgetShow(QString &btnName);
    void createSettingTree();
    void createLayout();

    
    void initLogSettings(); // setting dialog load
    void browseLogPath();
    void initVideoSettings();
    
    void createAudioPage();
    void createVideoPage();
    void createHardwarePage();
    void findUvcCameraDevices();
    void applyHardwareSetting();
    void onCheckBoxStateChanged(int state);
    std::array<bool, 4> extractBits(QString hexString);
    
    QByteArray convertCheckBoxValueToBytes();
    void initHardwareSetting();
    void createPages();
    
    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void createButtons();
    void applyAccrodingPage();
    void setLogCheckBox();
    void handleOkButton();

    // video setting
    void populateResolutionBox(const QList<QCameraFormat> &videoFormats);
    void setFpsRange(const std::set<int> &fpsValues);
    QVariant boxValue(const QComboBox *) const;
    void applyVideoSettings();
    void updatePixelFormats();
    QCameraFormat getVideoFormat(const QSize &resolution, int desiredFrameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
};

#endif // SETTINGDIALOG_H
