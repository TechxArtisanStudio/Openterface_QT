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

#include "logpage.h"
#include <QFileDialog>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QSettings>
#include <QLoggingCategory>
#include <QHeaderView>
#include <QMessageBox>
#include "global.h"
#include "../globalsetting.h"
#include "ui/loghandler.h"
#include "log/logcategoryregistry.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDialog>

// Category metadata: friendly names and default levels
static struct CategoryMeta {
    const char* category;
    const char* displayName;
    const char* defaultLevel;
} s_categoryMeta[] = {
    // === Serial ===
    {"opf.core.serial.tx",         "TX Data",              "Debug"},
    {"opf.core.serial.rx",         "RX Data",              "Debug"},
    {"opf.core.serial.cmd",        "Command",              "Info"},
    {"opf.core.serial.conn",       "Connection",           "Info"},
    {"opf.core.serial.watchdog",   "Watchdog",             "Warning"},
    {"opf.core.serial.hotplug",    "Hotplug",              "Info"},
    {"opf.core.serial.config",     "Config",               "Info"},
    {"opf.core.serial.lockkeys",   "Lock Keys",            "Debug"},
    {"opf.core.serial.usbswitch",  "USB Switch",           "Debug"},
    {"opf.serial.state",           "State",                "Info"},
    {"opf.serial.statistics",      "Statistics",           "Debug"},
    // === Input (Keyboard + Mouse) ===
    {"opf.host.keyboard.mapping",   "Key Mapping",         "Debug"},
    {"opf.host.keyboard.modifiers", "Modifiers",           "Debug"},
    {"opf.host.keyboard.ime",       "IME",                 "Debug"},
    {"opf.host.keyboard.special",   "Special Keys",        "Info"},
    {"opf.host.keyboard.state",     "Key State",           "Info"},
    {"opf.host.layouts",            "Layouts",             "Info"},
    {"opf.host.mouse.absolute",     "Absolute",            "Debug"},
    {"opf.host.mouse.relative",     "Relative",            "Debug"},
    {"opf.host.mouse.scroll",       "Scroll",              "Info"},
    // === HID / Chip ===
    {"opf.core.hid.detect",        "Detection",            "Info"},
    {"opf.core.hid.poll",          "Polling",              "Info"},
    {"opf.core.hid.firmware",      "Firmware",             "Info"},
    {"opf.core.hid.device",        "Device",               "Info"},
    {"opf.core.chip.read",         "Register Read",        "Debug"},
    {"opf.core.chip.flash",        "Flash",                "Info"},
    {"opf.core.chip.gpio",         "GPIO",                 "Debug"},
    {"opf.host.win_transport",     "Win Transport",        "Debug"},
    {"opf.host.linux_transport",   "Linux Transport",      "Debug"},
    // === Device ===
    {"opf.device.manager",         "Manager",              "Info"},
    {"opf.device.factory",         "Factory",              "Info"},
    {"opf.device.hotplug",         "Hotplug",              "Info"},
    {"opf.device.linux",           "Linux",                "Info"},
    {"opf.device.debounce",        "Debounce",             "Info"},
    {"opf.host.windows",           "Windows",              "Info"},
    {"opf.host.windows.enumerator","Enumerator",           "Info"},
    {"opf.host.windows.discoverer","Discoverer",           "Info"},
    // === Camera / Backend ===
    {"opf.ui.camera",              "Camera",               "Info"},
    {"opf.backend",                "Backend",              "Info"},
    {"opf.backend.ffmpeg",         "FFmpeg",               "Info"},
    {"opf.backend.qt",             "Qt Multimedia",        "Info"},
    {"opf.backend.qtmultimedia",   "Qt Backend",           "Info"},
    {"opf.multimedia.backend",     "Multimedia",           "Info"},
    {"opf.backend.gstreamer",      "GStreamer",            "Info"},
    {"opf.backend.gstreamerhelpers","Helpers",             "Debug"},
    {"opf.backend.gstreamer.runner.external","Runner Ext",  "Debug"},
    {"opf.backend.gstreamer.runner.inprocess","Runner In-Proc","Debug"},
    {"opf.backend.gstreamer.pipelinefactory","Pipeline Factory","Debug"},
    {"opf.backend.gstreamer.pipelinebuilder","Pipeline Builder","Debug"},
    {"opf.backend.gstreamer.queueconfigurator","Queue Config","Debug"},
    {"opf.backend.gstreamer.sinkselector","Sink Selector",  "Debug"},
    {"opf.backend.gstreamer.recording","Recording",         "Info"},
    {"opf.backend.videooverlaymanager","Video Overlay",     "Debug"},
    // === Audio ===
    {"opf.core.audio",             "Audio Core",           "Info"},
    {"opf.core.host.audio",        "Host Audio",           "Info"},
    // === Scripts ===
    {"opf.scripts",                "Scripts",              "Info"},
    {"opf.scripts.scriptrunner",   "Runner",               "Info"},
    {"opf.ui.scriptexec",          "Executor",             "Info"},
    // === Server ===
    {"opf.server.tcp",             "TCP",                  "Info"},
    {"opf.server.tcp.response",    "TCP Response",         "Debug"},
    {"opf.server.mcp",             "MCP",                  "Info"},
    {"opf.server.mcp.protocol",    "MCP Protocol",         "Debug"},
    {"opf.server.mcp.sse",         "MCP SSE",              "Debug"},
    {"opf.server.mcp.tool",        "MCP Tool",             "Debug"},
    // === System ===
    {"opf.systemkey",              "SysKey Blocker",       "Info"},
    {"opf.systemkey.win",          "SysKey Windows",       "Info"},
    {"opf.systemkey.x11",          "SysKey X11",           "Info"},
    {"opf.usb",                    "USB Control",          "Info"},
    {"opf.targetcontrol",          "Target Control",       "Info"},
    // === UI ===
    {"opf.ui.mainwindow",          "Main Window",          "Info"},
    {"opf.ui.mainwindowinitializer","Initializer",         "Info"},
    {"opf.ui.input",               "Input",                "Info"},
    {"opf.ui.video",               "Video",                "Info"},
    {"opf.ui.statuswidget",        "Status Widget",        "Info"},
    {"opf.ui.statusbarmanager",    "Status Bar",           "Info"},
    {"opf.ui.windowcontrolmanager","Window Control",       "Info"},
    {"opf.ui.recordingcontroller", "Recording",            "Info"},
    {"opf.ui.deviceselector",      "Device Selector",      "Info"},
    {"opf.ui.keyboardeditor",      "Keyboard Editor",      "Info"},
    {"opf.ui.customkeydialog",     "Custom Key Dialog",    "Debug"},
    {"opf.ui.customkeys",          "Custom Keys",          "Info"},
    {"opf.ui.devicecoordinator",   "Device Coord",         "Info"},
    {"opf.ui.menucoordinator",     "Menu Coord",           "Info"},
    {"opf.ui.windowlayoutcoordinator","Window Layout Coord","Info"},
    {"opf.ui.audio.page",          "Audio Page",           "Info"},
    {"opf.ui.mcp.page",            "MCP Page",             "Info"},
    {"opf.diagnostics",            "Diagnostics",          "Info"},
    {"opf.video.recording",        "Video Recording",      "Info"},
    // === AI Chat ===
    {"openterface.ai.chat",        "AI Chat",              "Debug"},
};

static const CategoryMeta* findCategoryMeta(const QString& category) {
    for (const auto& meta : s_categoryMeta) {
        if (category == meta.category) {
            return &meta;
        }
    }
    return nullptr;
}

static QString getFriendlyName(const QString& category) {
    const CategoryMeta* meta = findCategoryMeta(category);
    if (meta) {
        return QString::fromUtf8(meta->displayName);
    }
    // Fallback: use last segment
    QStringList parts = category.split('.');
    return parts.last();
}

static QString getDefaultLevel(const QString& category) {
    const CategoryMeta* meta = findCategoryMeta(category);
    return meta ? QString::fromUtf8(meta->defaultLevel) : "Info";
}

LogPage::LogPage(QWidget *parent) : PreferencePageBase(parent)
{
    setupUI();
    initLogSettings();
}

void LogPage::setupUI()
{
    // Log file controls
    storeLogCheckBox = new QCheckBox(tr("Enable file logging"));
    storeLogCheckBox->setObjectName("storeLogCheckBox");
    logFilePathLineEdit = new QLineEdit(this);
    logFilePathLineEdit->setObjectName("logFilePathLineEdit");
    browseButton = new QPushButton(tr("Browse"));
    browseButton->setObjectName("browseButton");

    // Category tree view
    selectAllCheckBox = new QCheckBox(tr("Select All"));
    selectAllCheckBox->setObjectName("selectAllCheckBox");
    selectAllCheckBox->setChecked(true);

    categoryTreeView = new QTreeView(this);
    categoryTreeView->setObjectName("categoryTreeView");
    categoryTreeView->setRootIsDecorated(true);
    categoryTreeView->setAlternatingRowColors(false);
    categoryTreeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    categoryTreeView->setMaximumHeight(280);
    categoryTreeView->setIndentation(20);

    categoryModel = new QStandardItemModel(this);
    categoryModel->setColumnCount(3);
    categoryModel->setHorizontalHeaderLabels({tr("Category"), tr("Level"), tr("ID")});

    categoryTreeView->setModel(categoryModel);
    categoryTreeView->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    categoryTreeView->header()->setStretchLastSection(true);

    // Other settings (unchanged)
    screenSaverCheckBox = new QCheckBox(tr("Inhibit Screen Saver"));
    screenSaverCheckBox->setObjectName("screenSaverCheckBox");
    hideKeyboardInputCheckBox = new QCheckBox(tr("Hide keyboard input characters"));
    hideKeyboardInputCheckBox->setObjectName("hideKeyboardInputCheckBox");
    floatingWindowCheckBox = new QCheckBox(tr("Show floating control window"));
    floatingWindowCheckBox->setObjectName("floatingWindowCheckBox");

    floatingWindowOpacitySlider = new QSlider(Qt::Horizontal, this);
    floatingWindowOpacitySlider->setRange(20, 100);
    floatingWindowOpacitySlider->setObjectName("floatingWindowOpacitySlider");
    floatingWindowOpacityLabel = new QLabel(tr("Opacity: 85%"));
    floatingWindowOpacityLabel->setObjectName("floatingWindowOpacityLabel");
    floatingWindowOpacityLabel->setStyleSheet(commentsFontSize);

    systemKeyBlockerCheckBox = new QCheckBox(tr("Enable System Key Blocker"));
    systemKeyBlockerCheckBox->setObjectName("systemKeyBlockerCheckBox");

    // Layout
    QHBoxLayout *logFilePathLayout = new QHBoxLayout();
    logFilePathLayout->addWidget(logFilePathLineEdit);
    logFilePathLayout->addWidget(browseButton);

    QLabel *logLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Log category levels")));
    logLabel->setTextFormat(Qt::RichText);
    logLabel->setStyleSheet(bigLabelFontSize);

    QLabel *logDescription = new QLabel(tr("Use the tree below to enable/disable categories. Each category has a preset log level."));
    logDescription->setStyleSheet(commentsFontSize);

    connect(browseButton, &QPushButton::clicked, this, &LogPage::browseLogPath);

    // Model changed signal — handle group checkbox propagation
    // Use a re-entrancy guard instead of blockSignals so the view updates correctly
    // Skip group propagation while programmatically restoring state (m_restoring)
    static bool propagating = false;
    connect(categoryModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item){
        if (propagating || m_restoring) return;

        // If changed item is a group (has children), propagate check state to all children
        if (item && item->rowCount() > 0 && item->isCheckable()) {
            Qt::CheckState groupState = item->checkState();

            // Check if all children already match this state
            bool allMatch = true;
            bool anyChecked = false;
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem* child = item->child(c, 0);
                if (child && child->isCheckable()) {
                    if (child->checkState() != groupState) allMatch = false;
                    if (child->checkState() == Qt::Checked) anyChecked = true;
                }
            }

            // Determine desired state:
            // - All children match group → user clicked to toggle opposite
            // - PartiallyChecked (auto-tristate) → check all if any checked, else uncheck all
            // - Otherwise → propagate group state
            Qt::CheckState desiredState;
            if (allMatch) {
                desiredState = (groupState == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
            } else if (groupState == Qt::PartiallyChecked) {
                desiredState = anyChecked ? Qt::Checked : Qt::Unchecked;
            } else {
                desiredState = groupState;
            }

            // Guard against recursive signals while updating children
            propagating = true;
            item->setCheckState(desiredState);
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem* child = item->child(c, 0);
                if (child && child->isCheckable()) {
                    child->setCheckState(desiredState);
                }
            }
            propagating = false;
        }

        // Sync selectAll checkbox: checked only when ALL leaf items are checked
        bool allChecked = true;
        for (int g = 0; g < categoryModel->rowCount(); ++g) {
            QStandardItem* grp = categoryModel->item(g);
            for (int c = 0; c < grp->rowCount(); ++c) {
                QStandardItem* child = grp->child(c, 0);
                if (child && child->isCheckable() && child->checkState() != Qt::Checked) {
                    allChecked = false;
                    break;
                }
            }
            if (!allChecked) break;
        }
        selectAllCheckBox->blockSignals(true);
        selectAllCheckBox->setChecked(allChecked);
        selectAllCheckBox->blockSignals(false);

        checkDirtyState();
    });

    // Select All checkbox — toggle all groups and their children
    connect(selectAllCheckBox, &QCheckBox::toggled, this, [this](bool checked){
        propagating = true;
        Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
        for (int g = 0; g < categoryModel->rowCount(); ++g) {
            QStandardItem* group = categoryModel->item(g);
            if (group && group->isCheckable()) {
                group->setCheckState(state);
            }
            for (int c = 0; c < group->rowCount(); ++c) {
                QStandardItem* child = group->child(c, 0);
                if (child && child->isCheckable()) {
                    child->setCheckState(state);
                }
            }
        }
        propagating = false;
        checkDirtyState();
    });

    // Main layout
    QVBoxLayout *logLayout = new QVBoxLayout(this);
    logLayout->addWidget(logLabel);
    logLayout->addWidget(logDescription);
    logLayout->addWidget(selectAllCheckBox);
    logLayout->addWidget(categoryTreeView);
    logLayout->addWidget(storeLogCheckBox);
    logLayout->addLayout(logFilePathLayout);

    // Screen Saver section
    QLabel *screenSaverLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Screen Saver setting")));
    screenSaverLabel->setTextFormat(Qt::RichText);
    screenSaverLabel->setStyleSheet(bigLabelFontSize);

    QLabel *screenSaverDescription = new QLabel(tr("Inhibit the screen saver when the application is running."));
    screenSaverDescription->setStyleSheet(commentsFontSize);

    logLayout->addWidget(screenSaverLabel);
    logLayout->addWidget(screenSaverDescription);
    logLayout->addWidget(screenSaverCheckBox);

    // Keyboard Input section
    QLabel *keyboardInputLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Keyboard Input privacy")));
    keyboardInputLabel->setTextFormat(Qt::RichText);
    keyboardInputLabel->setStyleSheet(bigLabelFontSize);

    QLabel *keyboardInputDescription = new QLabel(tr("Hide keyboard input characters in the status bar by displaying them as dots."));
    keyboardInputDescription->setStyleSheet(commentsFontSize);

    logLayout->addWidget(keyboardInputLabel);
    logLayout->addWidget(keyboardInputDescription);
    logLayout->addWidget(hideKeyboardInputCheckBox);

    // Floating Window section
    QLabel *floatingWindowLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("Floating control window")));
    floatingWindowLabel->setTextFormat(Qt::RichText);
    floatingWindowLabel->setStyleSheet(bigLabelFontSize);

    QLabel *floatingWindowDescription = new QLabel(tr("Show a floating window with video control buttons."));
    floatingWindowDescription->setStyleSheet(commentsFontSize);

    logLayout->addWidget(floatingWindowLabel);
    logLayout->addWidget(floatingWindowDescription);
    logLayout->addWidget(floatingWindowCheckBox);

    QHBoxLayout *opacityLayout = new QHBoxLayout();
    opacityLayout->addWidget(floatingWindowOpacityLabel);
    opacityLayout->addWidget(floatingWindowOpacitySlider);
    logLayout->addLayout(opacityLayout);

    // System Key Blocker section
    QLabel *systemKeyBlockerLabel = new QLabel(QString("<span style='font-weight: bold;'>%1</span>").arg(tr("System Key Blocker")));
    systemKeyBlockerLabel->setTextFormat(Qt::RichText);
    systemKeyBlockerLabel->setStyleSheet(bigLabelFontSize);

    QLabel *systemKeyBlockerDescription = new QLabel(
        tr("When enabled, intercepts ALL keystrokes at the OS level (Win/Super, "
           "PrintScreen, Alt+Tab, and other system keys) and forwards them to the "
           "target machine instead. The host OS will not receive these keys while "
           "the blocker is active.\n\n"
           "\xe2\x9a\xa0 Use with caution \xe2\x80\x94 only enable this when the video pane has focus."));
    systemKeyBlockerDescription->setWordWrap(true);
    systemKeyBlockerDescription->setStyleSheet(commentsFontSize);

    logLayout->addWidget(systemKeyBlockerLabel);
    logLayout->addWidget(systemKeyBlockerDescription);
    logLayout->addWidget(systemKeyBlockerCheckBox);

    // Button bar via base class
    createButtonBar(logLayout);

    // Connect setting widgets to markDirty()
    connect(storeLogCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });
    connect(logFilePathLineEdit, &QLineEdit::textChanged, this, [this]{ checkDirtyState(); });
    connect(screenSaverCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });
    connect(hideKeyboardInputCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });
    connect(floatingWindowCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });
    connect(floatingWindowOpacitySlider, &QSlider::valueChanged, this, [this]{ checkDirtyState(); });
    connect(systemKeyBlockerCheckBox, &QCheckBox::toggled, this, [this]{ checkDirtyState(); });

    logLayout->addStretch();
}

void LogPage::populateCategoryTree()
{
    // Clear existing items
    categoryModel->removeRows(0, categoryModel->rowCount());

    // Build group maps
    QMap<QString, QStringList> groupMap;  // group name -> category names
    QStringList allCategories = LogCategoryRegistry::instance().allCategories();

    QStringList uncategorized;

    for (const QString &cat : allCategories) {
        bool placed = false;

        // Serial: opf.core.serial.* or opf.serial.*
        if (cat.startsWith("opf.core.serial.") || cat.startsWith("opf.serial.")) {
            groupMap["Serial"].append(cat);
            placed = true;
        }
        // Input (Keyboard + Mouse): opf.host.keyboard.* or opf.host.layouts* or opf.host.mouse.*
        else if (cat.startsWith("opf.host.keyboard.") || cat.startsWith("opf.host.layouts")
                 || cat.startsWith("opf.host.mouse.")) {
            groupMap["Input"].append(cat);
            placed = true;
        }
        // HID/Chip: opf.core.hid.* or opf.core.chip.* or opf.host.*_transport
        else if (cat.startsWith("opf.core.hid.") || cat.startsWith("opf.core.chip.")
                 || cat.endsWith("_transport") || cat.contains("_transport.")) {
            groupMap["HID/Chip"].append(cat);
            placed = true;
        }
        // Device: opf.device.* or opf.host.windows* (device enumerators)
        else if (cat.startsWith("opf.device.") || cat.startsWith("opf.host.windows")) {
            groupMap["Device"].append(cat);
            placed = true;
        }
        // Camera/Backend: opf.backend.* or opf.multimedia.* or opf.ui.camera
        else if (cat.startsWith("opf.backend.") || cat == "opf.backend"
                 || cat.startsWith("opf.multimedia.") || cat == "opf.ui.camera") {
            groupMap["Camera/Backend"].append(cat);
            placed = true;
        }
        // Audio: opf.core.audio* or opf.core.host.audio*
        else if (cat.startsWith("opf.core.audio") || cat.startsWith("opf.core.host.audio")) {
            groupMap["Audio"].append(cat);
            placed = true;
        }
        // Scripts: opf.scripts.* or opf.ui.scriptexec
        else if (cat.startsWith("opf.scripts.") || cat == "opf.scripts" || cat == "opf.ui.scriptexec") {
            groupMap["Scripts"].append(cat);
            placed = true;
        }
        // Server: opf.server.*
        else if (cat.startsWith("opf.server.")) {
            groupMap["Server"].append(cat);
            placed = true;
        }
        // System: opf.systemkey.* or opf.usb* or opf.targetcontrol*
        else if (cat.startsWith("opf.systemkey") || cat.startsWith("opf.usb")
                 || cat.startsWith("opf.targetcontrol")) {
            groupMap["System"].append(cat);
            placed = true;
        }
        // UI: opf.ui.* or opf.diagnostics* or opf.video.recording*
        else if (cat.startsWith("opf.ui.") || cat.startsWith("opf.diagnostics")
                 || cat.startsWith("opf.video.")) {
            groupMap["UI"].append(cat);
            placed = true;
        }
        // AI Chat: openterface.ai.*
        else if (cat.startsWith("openterface.ai.")) {
            groupMap["AI Chat"].append(cat);
            placed = true;
        }

        if (!placed) {
            uncategorized.append(cat);
        }
    }

    // If there are uncategorized categories, put them in "Other"
    if (!uncategorized.isEmpty()) {
        groupMap["Other"] = uncategorized;
    }

    // Map group names to display names
    QMap<QString, QString> groupDisplayNames = {
        {"Serial", "Serial"},
        {"Input", "Input (Keyboard & Mouse)"},
        {"HID/Chip", "HID / Chip"},
        {"Device", "Device"},
        {"Camera/Backend", "Camera / Backend"},
        {"Audio", "Audio"},
        {"Scripts", "Scripts"},
        {"Server", "Server"},
        {"System", "System"},
        {"UI", "UI"},
        {"AI Chat", "AI Chat"},
        {"Other", "Other"}
    };

    // Create top-level groups in a stable order
    QStringList groupOrder = {"Serial", "Input", "HID/Chip", "Device", "Camera/Backend",
                              "Audio", "Scripts", "Server", "System", "UI", "AI Chat", "Other"};
    for (const QString &groupName : groupOrder) {
        if (!groupMap.contains(groupName)) continue;
        const QStringList &cats = groupMap[groupName];
        if (cats.isEmpty()) continue;

        QString displayName = groupDisplayNames.value(groupName, groupName);
        QStandardItem *groupItem = new QStandardItem(displayName);
        groupItem->setCheckable(true);
        groupItem->setCheckState(Qt::Checked);
        groupItem->setEditable(false);
        QFont f = groupItem->font();
        f.setBold(true);
        groupItem->setFont(f);

        for (const QString &cat : cats) {
            QString friendlyName = getFriendlyName(cat);
            QString defaultLevel = getDefaultLevel(cat);

            QStandardItem *nameItem = new QStandardItem(friendlyName);
            nameItem->setCheckable(true);
            nameItem->setCheckState(Qt::Checked);
            nameItem->setEditable(false);
            // Store raw category in user data
            nameItem->setData(cat, Qt::UserRole + 1);

            QStandardItem *levelItem = new QStandardItem(defaultLevel);
            levelItem->setEditable(false);

            QStandardItem *idItem = new QStandardItem(cat);
            idItem->setEditable(false);
            idItem->setForeground(QColor(128, 128, 128));

            groupItem->appendRow({nameItem, levelItem, idItem});
        }

        categoryModel->appendRow(groupItem);
    }

    categoryTreeView->collapseAll();
}

QString LogPage::generateFilterRules() const
{
    QStringList rules;
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            bool enabled = nameItem->checkState() == Qt::Checked;
            QString level = levelItem->text();

            if (!enabled) {
                rules << QString("%1=false").arg(category);
            } else {
                // Map level text to QLoggingCategory level names
                QString levelLower = level.toLower().trimmed();
                if (levelLower == "off") {
                    rules << QString("%1=false").arg(category);
                } else {
                    // e.g., opf.core.serial.tx.debug=true
                    rules << QString("%1.%2=true").arg(category, levelLower);
                }
            }
        }
    }
    return rules.join('\n');
}

void LogPage::saveCategorySettings() const
{
    QMap<QString, QPair<bool, QString>> states;
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            bool enabled = nameItem->checkState() == Qt::Checked;
            QString level = levelItem->text();
            states.insert(category, qMakePair(enabled, level));
        }
    }
    GlobalSetting::instance().saveCategoryStates(states);
}

void LogPage::restoreCategorySettings()
{
    auto states = GlobalSetting::instance().loadCategoryStates();

    // Suppress group-propagation handler during bulk restore
    m_restoring = true;
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            QString defaultLevel = getDefaultLevel(category);
            if (states.contains(category)) {
                auto state = states.value(category);
                nameItem->setCheckState(state.first ? Qt::Checked : Qt::Unchecked);
                levelItem->setText(state.second);
            } else {
                levelItem->setText(defaultLevel);
            }
        }
    }

    // Recalculate group check states (must stay inside m_restoring=true
    // to prevent the handler from propagating and overwriting children)
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g, 0);
        bool allChildrenChecked = true;
        bool anyChildChecked = false;
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable()) {
                if (child->checkState() == Qt::Checked) anyChildChecked = true;
                else allChildrenChecked = false;
            }
        }
        if (allChildrenChecked) group->setCheckState(Qt::Checked);
        else if (anyChildChecked) group->setCheckState(Qt::PartiallyChecked);
        else group->setCheckState(Qt::Unchecked);
    }
    m_restoring = false;

    // Sync selectAll checkbox
    bool allChecked = true;
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable() && child->checkState() != Qt::Checked) {
                allChecked = false;
                break;
            }
        }
        if (!allChecked) break;
    }
    selectAllCheckBox->blockSignals(true);
    selectAllCheckBox->setChecked(allChecked);
    selectAllCheckBox->blockSignals(false);
}

void LogPage::browseLogPath()
{
    QString exeDir = QCoreApplication::applicationDirPath();
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Log Directory"),
                                                    exeDir,
                                                    QFileDialog::ShowDirsOnly
                                                    | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        QString logPath = dir + "/openterface_log.txt";
        logFilePathLineEdit->setText(dir);
        QFile file(logPath);
        if (!file.exists()) {
            if (file.open(QIODevice::WriteOnly)) {
                file.close();
            } else {
                qWarning() << "Failed to create log file:" << logPath;
            }
        }
        logFilePathLineEdit->setText(logPath);
    }
}

void LogPage::initLogSettings()
{
    QSettings settings("Techxartisan", "Openterface");

    // Populate the category tree from the registry
    populateCategoryTree();
    restoreCategorySettings();

    // Other settings
    storeLogCheckBox->setChecked(settings.value("log/storeLog", false).toBool());
    screenSaverCheckBox->setChecked(settings.value("ScreenSaver/Inhibited", false).toBool());

    hideKeyboardInputCheckBox->setChecked(GlobalSetting::instance().getHideKeyboardInput());
    floatingWindowCheckBox->setChecked(GlobalSetting::instance().getFloatingWindowEnabled());

    int opacityValue = GlobalSetting::instance().getFloatingWindowOpacity() * 100;
    floatingWindowOpacitySlider->setValue(qBound(20, opacityValue, 100));
    floatingWindowOpacityLabel->setText(tr("Opacity: %1%").arg(floatingWindowOpacitySlider->value()));
    connect(floatingWindowOpacitySlider, &QSlider::valueChanged, this, [this](int val) {
        floatingWindowOpacityLabel->setText(tr("Opacity: %1%").arg(val));
    });

    systemKeyBlockerCheckBox->setChecked(GlobalSetting::instance().getSystemKeyBlockerEnabled());
    logFilePathLineEdit->setText(settings.value("log/logFilePath", "").toString());

    // Apply initial filter rules
    QLoggingCategory::setFilterRules(generateFilterRules());

    captureSnapshot();
    clearDirty();
}

void LogPage::applySettings()
{
    // Apply filter rules
    QString logFilter = generateFilterRules();
    QLoggingCategory::setFilterRules(logFilter);

    // Save category states
    saveCategorySettings();

    // File logging
    bool storeLog = storeLogCheckBox->isChecked();
    QString logFilePath = logFilePathLineEdit->text();
    LogHandler::instance().setFileLoggingEnabled(storeLog, logFilePath);

    // Also persist log file path
    QSettings settings("Techxartisan", "Openterface");
    settings.setValue("log/storeLog", storeLog);
    settings.setValue("log/logFilePath", logFilePath);

    // Screen saver
    bool inhibitScreenSaver = screenSaverCheckBox->isChecked();
    settings.setValue("ScreenSaver/Inhibited", inhibitScreenSaver);
    emit ScreenSaverInhibitedChanged(inhibitScreenSaver);

    // Hide keyboard input
    bool hideKeyboardInput = hideKeyboardInputCheckBox->isChecked();
    GlobalSetting::instance().setHideKeyboardInput(hideKeyboardInput);
    emit hideKeyboardInputChanged(hideKeyboardInput);

    // Floating window
    bool floatingWindowEnabled = floatingWindowCheckBox->isChecked();
    GlobalSetting::instance().setFloatingWindowEnabled(floatingWindowEnabled);
    emit floatingWindowEnabledChanged(floatingWindowEnabled);

    double opacity = floatingWindowOpacitySlider->value() / 100.0;
    GlobalSetting::instance().setFloatingWindowOpacity(opacity);
    emit floatingWindowOpacityChanged(opacity);

    // System key blocker
    bool systemKeyBlockerEnabled = systemKeyBlockerCheckBox->isChecked();
    GlobalSetting::instance().setSystemKeyBlockerEnabled(systemKeyBlockerEnabled);
    emit systemKeyBlockerToggled(systemKeyBlockerEnabled);
}

void LogPage::captureSnapshot()
{
    m_snap_storeLog = storeLogCheckBox->isChecked();
    m_snap_logFilePath = logFilePathLineEdit->text();
    m_snap_screenSaver = screenSaverCheckBox->isChecked();
    m_snap_hideKeyboardInput = hideKeyboardInputCheckBox->isChecked();
    m_snap_floatingWindow = floatingWindowCheckBox->isChecked();
    m_snap_floatingWindowOpacity = floatingWindowOpacitySlider->value();
    m_snap_systemKeyBlocker = systemKeyBlockerCheckBox->isChecked();

    // Capture tree state
    m_snap_categoryStates.clear();
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            bool enabled = nameItem->checkState() == Qt::Checked;
            QString level = levelItem->text();
            m_snap_categoryStates.insert(category, qMakePair(enabled, level));
        }
    }
}

void LogPage::revertToSnapshot()
{
    storeLogCheckBox->setChecked(m_snap_storeLog);
    logFilePathLineEdit->setText(m_snap_logFilePath);
    screenSaverCheckBox->setChecked(m_snap_screenSaver);
    hideKeyboardInputCheckBox->setChecked(m_snap_hideKeyboardInput);
    floatingWindowCheckBox->setChecked(m_snap_floatingWindow);
    floatingWindowOpacitySlider->setValue(m_snap_floatingWindowOpacity);
    systemKeyBlockerCheckBox->setChecked(m_snap_systemKeyBlocker);

    // Restore tree state — suppress group-propagation handler
    m_restoring = true;
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            if (m_snap_categoryStates.contains(category)) {
                auto state = m_snap_categoryStates.value(category);
                nameItem->setCheckState(state.first ? Qt::Checked : Qt::Unchecked);
                levelItem->setText(state.second);
            }
        }
    }

    // Recalculate group check states (must stay inside m_restoring=true
    // to prevent the handler from propagating and overwriting children)
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g, 0);
        bool allChildrenChecked = true;
        bool anyChildChecked = false;
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable()) {
                if (child->checkState() == Qt::Checked) anyChildChecked = true;
                else allChildrenChecked = false;
            }
        }
        if (allChildrenChecked) group->setCheckState(Qt::Checked);
        else if (anyChildChecked) group->setCheckState(Qt::PartiallyChecked);
        else group->setCheckState(Qt::Unchecked);
    }
    m_restoring = false;

    // Sync selectAll checkbox
    bool allChecked = true;
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *child = group->child(c, 0);
            if (child && child->isCheckable() && child->checkState() != Qt::Checked) {
                allChecked = false;
                break;
            }
        }
        if (!allChecked) break;
    }
    selectAllCheckBox->blockSignals(true);
    selectAllCheckBox->setChecked(allChecked);
    selectAllCheckBox->blockSignals(false);
}

bool LogPage::valuesMatchSnapshot() const
{
    if (storeLogCheckBox->isChecked() != m_snap_storeLog) return false;
    if (logFilePathLineEdit->text() != m_snap_logFilePath) return false;
    if (screenSaverCheckBox->isChecked() != m_snap_screenSaver) return false;
    if (hideKeyboardInputCheckBox->isChecked() != m_snap_hideKeyboardInput) return false;
    if (floatingWindowCheckBox->isChecked() != m_snap_floatingWindow) return false;
    if (floatingWindowOpacitySlider->value() != m_snap_floatingWindowOpacity) return false;
    if (systemKeyBlockerCheckBox->isChecked() != m_snap_systemKeyBlocker) return false;

    // Check tree state
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            bool enabled = nameItem->checkState() == Qt::Checked;
            QString level = levelItem->text();
            if (!m_snap_categoryStates.contains(category)) return false;
            auto expected = m_snap_categoryStates.value(category);
            if (enabled != expected.first) return false;
            if (level != expected.second) return false;
        }
    }
    return true;
}
