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
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>


namespace Ui {
class SettingDialog;
}

class SettingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingDialog(QWidget *parent = nullptr);
    ~SettingDialog();

private:
    Ui::SettingDialog *ui;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    QWidget *logPage;
    QWidget *videoPage;
    QWidget *audioPage;
    QWidget *buttonWidget;

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
};

#endif // SETTINGDIALOG_H
