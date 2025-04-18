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
#include <QMediaDevices>
#include <QByteArray>
#include <QMap>
#include <QCheckBox>
#include <QLineEdit>
#include <QByteArray>
#include "host/cameramanager.h"
#include "logpage.h"
#include "targetcontrolpage.h"
#include "videopage.h"
#include "audiopage.h"
QT_BEGIN_NAMESPACE
class QCameraFormat;
class QComboBox;
class QCamera;
namespace Ui {
class SettingDialog;
}
QT_END_NAMESPACE

class SettingDialog : public QDialog
{
    Q_OBJECT

public:
    // Change the constructor to accept CameraManager instead of QCamera
    explicit SettingDialog(CameraManager *cameraManager, QWidget *parent = nullptr);
    ~SettingDialog();
    TargetControlPage* getTargetControlPage();
    VideoPage* getVideoPage();

    LogPage* getLogPage();

// signals:
//     // void serialSettingsApplied();
    
private:

    Ui::SettingDialog *ui;
    CameraManager *m_cameraManager;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    LogPage *logPage;
    QWidget *audioPage;
    VideoPage *videoPage;
    TargetControlPage *targetControlPage;

    QWidget *buttonWidget;

    void createSettingTree();
    void createLayout();
    void createPages();
    
    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void createButtons();
    void applyAccrodingPage();
    void handleOkButton();
};

#endif // SETTINGDIALOG_H
