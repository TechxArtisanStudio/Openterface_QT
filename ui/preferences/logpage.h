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

#ifndef LOGPAGE_H
#define LOGPAGE_H

#include <QWidget>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QSlider>
#include <QTreeView>
#include <QStandardItemModel>
#include <QComboBox>
#include <QMap>
#include <QPair>
#include "fontstyle.h"
#include "preferencepagebase.h"

class LogPage : public PreferencePageBase
{
    Q_OBJECT

public:
    explicit LogPage(QWidget *parent = nullptr);
    void setupUI();
    void browseLogPath();
    void initLogSettings();
    void applySettings() override;
    void captureSnapshot() override;
    bool valuesMatchSnapshot() const override;
    void revertToSnapshot() override;

signals:
    void ScreenSaverInhibitedChanged(bool inhibited);
    void hideKeyboardInputChanged(bool hide);
    void floatingWindowEnabledChanged(bool enabled);
    void floatingWindowOpacityChanged(double opacity);
    void systemKeyBlockerToggled(bool enabled);

private:
    void populateCategoryTree();
    QString generateFilterRules() const;
    void saveCategorySettings() const;
    void restoreCategorySettings();

    // Log file controls
    QCheckBox *storeLogCheckBox;
    QLineEdit *logFilePathLineEdit;
    QPushButton *browseButton;

    // Category tree view
    QTreeView *categoryTreeView;
    QStandardItemModel *categoryModel;
    QCheckBox *selectAllCheckBox;

    // Other settings (unchanged from original)
    QCheckBox *screenSaverCheckBox;
    QCheckBox *hideKeyboardInputCheckBox;
    QCheckBox *floatingWindowCheckBox;
    QSlider *floatingWindowOpacitySlider;
    QLabel *floatingWindowOpacityLabel;
    QCheckBox *systemKeyBlockerCheckBox;

    // Snapshot for revert
    bool m_snap_storeLog;
    QString m_snap_logFilePath;
    bool m_snap_screenSaver;
    bool m_snap_hideKeyboardInput;
    bool m_snap_floatingWindow;
    int m_snap_floatingWindowOpacity;
    bool m_snap_systemKeyBlocker;
    // Tree state snapshot: map of category -> {enabled, level}
    QMap<QString, QPair<bool, QString>> m_snap_categoryStates;

    // Guard flag: true while programmatically restoring tree state
    // (suppresses group-propagation in itemChanged handler)
    bool m_restoring = false;
};

#endif // LOGPAGE_H
