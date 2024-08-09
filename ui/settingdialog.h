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
#include <QCameraDevice>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <set>

QT_BEGIN_NAMESPACE
class QCameraFormat;
class QComboBox;
class QCamera;
namespace Ui {
class SettingDialog;
}
QT_END_NAMESPACE

// // Custom key structure
// struct VideoFormatKey {
//     QSize resolution;
//     int frameRate;
//     QVideoFrameFormat::PixelFormat pixelFormat;

//     bool operator<(const VideoFormatKey &other) const {
//         if (resolution.width() != other.resolution.width())
//             return resolution.width() < other.resolution.width();
//         if (resolution.height() != other.resolution.height())
//             return resolution.height() < other.resolution.height();
//         if (frameRate != other.frameRate)
//             return frameRate < other.frameRate;
//         return pixelFormat < other.pixelFormat;
//     }
// };

// struct QSizeComparator {
//     bool operator()(const QSize& lhs, const QSize& rhs) const {
//         if (lhs.width() == rhs.width()) {
//             return lhs.height() > rhs.height(); // Compare heights in descending order
//         }
//         return lhs.width() > rhs.width(); // Compare widths in descending order
//     }
// };

class SettingDialog : public QDialog
{
    Q_OBJECT

public:
    // explicit SettingDialog(QCamera *camera, QWidget *parent = nullptr);
    explicit SettingDialog(QCamera *camera, QWidget *parent = nullptr);
    ~SettingDialog();

private:
    Ui::SettingDialog *ui;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    QWidget *logPage;
    QWidget *videoPage;
    QWidget *audioPage;
    QWidget *buttonWidget;

    QCamera *camera; 
    // std::map<VideoFormatKey, QCameraFormat> videoFormatMap;
    // QCameraFormat getVideoFormat(const QSize &resolution, int frameRate, QVideoFrameFormat::PixelFormat pixelFormat) const;

    void switchWidgetShow(QString &btnName);
    void createSettingTree();
    void createLayout();
    void createLogPage();
    void createAudioPage();
    void createVideoPage();
    void createPages();
    
    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void createButtons();
    void readCheckBoxState();
    void applyAccrodingPage();
    void setLogCheckBox();
    void handleOkButton();

    // void populateResolutionBox(const QList<QCameraFormat> &videoFormats);
    void setFpsRange(const std::set<int> &fpsValues);
};

#endif // SETTINGDIALOG_H
