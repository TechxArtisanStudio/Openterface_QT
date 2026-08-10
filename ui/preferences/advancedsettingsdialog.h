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

#ifndef ADVANCEDSETTINGSDIALOG_H
#define ADVANCEDSETTINGSDIALOG_H

#include <QDialog>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>
#include <QWidget>
#include <QSplitter>
#include <QTimer>
#include "mcppage.h"
#include "firmwarepage.h"
#include "controlchipfirmwarepage.h"
#include "edidconfigpage.h"

class AdvancedSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AdvancedSettingsDialog(QWidget *parent = nullptr);
    ~AdvancedSettingsDialog();

    McpPage* getMcpPage();
    FirmwarePage* getFirmwarePage();

private:
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    FirmwarePage *firmwarePage;
    ControlChipFirmwarePage *controlChipFirmwarePage;
    McpPage *mcpPage;
    EdidConfigPage *edidConfigPage;

    QWidget *buttonWidget;
    QSplitter *splitter;
    int m_currentPageIndex;
    bool m_changingPage;
    QTimer *m_pageChangeTimer;

    void createSettingTree();
    void createLayout();
    void createPages();

    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void createButtons();
    void applyAccordingPage();
    void handleOkButton();
};

#endif // ADVANCEDSETTINGSDIALOG_H
