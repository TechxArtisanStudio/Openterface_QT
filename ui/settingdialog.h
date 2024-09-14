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

QT_BEGIN_NAMESPACE
class QCameraFormat;
class QComboBox;
class QCamera;
namespace Ui {
class SettingDialog;
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

class SettingDialog : public QDialog
{
    Q_OBJECT

public:
    // explicit SettingDialog(QCamera *camera, QWidget *parent = nullptr);
    explicit SettingDialog(QCamera *camera, QWidget *parent = nullptr);
    ~SettingDialog();

signals:
    void cameraSettingsApplied();
    void serialSettingsApplied();

private:
    
    const QString bigLabelFontSize = "QLabel { font-size: 14px; }";
    const QString smallLabelFontSize = "QLabel { font-size: 12px; }";
    const QString commentsFontSize = "QLabel { font-size: 10px; }";

    Ui::SettingDialog *ui;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    QWidget *logPage;
    QWidget *videoPage;
    QWidget *audioPage;
    QWidget *hardwarePage;
    QWidget *buttonWidget;


    QMap<QCheckBox *, QLineEdit *> USBCheckBoxEditMap; // map of checkboxes to line edit about VID PID etc.
    void addCheckBoxLineEditPair(QCheckBox *checkBox, QLineEdit *lineEdit);

    QCamera *camera; 
    QSize m_currentResolution;
    bool m_updatingFormats = false;
    std::map<VideoFormatKey, QCameraFormat> videoFormatMap;
    // QCameraFormat getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
    
    void switchWidgetShow(QString &btnName);
    void createSettingTree();
    void createLayout();
    void createLogPage();
    
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
    void applyLogsettings();
    void applyAccrodingPage();
    void setLogCheckBox();
    void handleOkButton();

    // video setting
    void populateResolutionBox(const QList<QCameraFormat> &videoFormats);
    void setFpsRange(const std::set<int> &fpsValues);
    QVariant boxValue(const QComboBox *) const;
    void onFpsSliderValueChanged(int value);
    void applyVideoSettings();
    void updatePixelFormats();
    QCameraFormat getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;
};

#endif // SETTINGDIALOG_H
