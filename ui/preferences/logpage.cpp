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
#include "ui/globalsetting.h"
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
    // Serial group
    {"opf.core.serial.tx",         "TX 数据",        "Debug"},
    {"opf.core.serial.rx",         "RX 数据",        "Debug"},
    {"opf.core.serial.cmd",        "命令协调",        "Info"},
    {"opf.core.serial.conn",       "连接状态",        "Info"},
    {"opf.core.serial.watchdog",   "看门狗",          "Warning"},
    {"opf.core.serial.hotplug",    "热插拔",          "Info"},
    {"opf.core.serial.config",     "芯片配置",        "Info"},
    {"opf.core.serial.lockkeys",   "锁定键",          "Debug"},
    {"opf.core.serial.usbswitch",  "USB 切换",       "Debug"},
    {"opf.serial.state",           "状态管理",        "Info"},
    {"opf.serial.statistics",      "统计",            "Debug"},
    // Keyboard group
    {"opf.host.keyboard.mapping",   "按键映射",       "Debug"},
    {"opf.host.keyboard.modifiers", "修饰键",         "Debug"},
    {"opf.host.keyboard.ime",       "输入法",         "Debug"},
    {"opf.host.keyboard.special",   "特殊键",         "Info"},
    {"opf.host.keyboard.state",     "按键状态",       "Info"},
    {"opf.host.layouts",            "键盘布局",       "Info"},
    // Mouse group
    {"opf.host.mouse.absolute",    "绝对坐标",        "Debug"},
    {"opf.host.mouse.relative",    "相对坐标",        "Debug"},
    {"opf.host.mouse.scroll",      "滚轮",            "Info"},
    // HID/Chip group
    {"opf.core.hid.detect",        "芯片检测",        "Info"},
    {"opf.core.hid.poll",          "轮询",            "Info"},
    {"opf.core.hid.firmware",      "固件",            "Info"},
    {"opf.core.hid.device",        "设备管理",        "Info"},
    {"opf.core.chip.read",         "寄存器读取",      "Debug"},
    {"opf.core.chip.flash",        "烧录",            "Info"},
    {"opf.core.chip.gpio",         "GPIO",            "Debug"},
    {"opf.host.win_transport",     "Windows 传输",    "Debug"},
    {"opf.host.linux_transport",   "Linux 传输",      "Debug"},
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
    categoryTreeView = new QTreeView(this);
    categoryTreeView->setObjectName("categoryTreeView");
    categoryTreeView->setRootIsDecorated(true);
    categoryTreeView->setAlternatingRowColors(true);
    categoryTreeView->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);

    categoryModel = new QStandardItemModel(this);
    categoryModel->setColumnCount(2);
    categoryModel->setHorizontalHeaderLabels({tr("Category"), tr("Level")});

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
    connect(categoryModel, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item){
        // If changed item is a group (has children), propagate check state
        if (item && item->rowCount() > 0 && item->isCheckable()) {
            Qt::CheckState groupState = item->checkState();
            bool blockSignalsState = categoryModel->blockSignals(true);
            for (int c = 0; c < item->rowCount(); ++c) {
                QStandardItem* child = item->child(c, 0);
                if (child && child->isCheckable()) {
                    child->setCheckState(groupState);
                }
            }
            categoryModel->blockSignals(blockSignalsState);
        }
        checkDirtyState();
    });

    // Main layout
    QVBoxLayout *logLayout = new QVBoxLayout(this);
    logLayout->addWidget(logLabel);
    logLayout->addWidget(logDescription);
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
        // Keyboard: opf.host.keyboard.* or opf.host.layouts*
        else if (cat.startsWith("opf.host.keyboard.") || cat.startsWith("opf.host.layouts")) {
            groupMap["Keyboard"].append(cat);
            placed = true;
        }
        // Mouse: opf.host.mouse.*
        else if (cat.startsWith("opf.host.mouse.")) {
            groupMap["Mouse"].append(cat);
            placed = true;
        }
        // HID/Chip: opf.core.hid.* or opf.core.chip.* or opf.host.*_transport
        else if (cat.startsWith("opf.core.hid.") || cat.startsWith("opf.core.chip.")
                 || cat.endsWith("_transport") || cat.contains("_transport.")) {
            groupMap["HID/Chip"].append(cat);
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

    // Map group names to Chinese display names
    QMap<QString, QString> groupDisplayNames = {
        {"Serial", "串口"},
        {"Keyboard", "键盘"},
        {"Mouse", "鼠标"},
        {"HID/Chip", "HID/芯片"},
        {"Other", "其他"}
    };

    // Create top-level groups in a stable order
    QStringList groupOrder = {"Serial", "Keyboard", "Mouse", "HID/Chip", "Other"};
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

            groupItem->appendRow({nameItem, levelItem});
        }

        categoryModel->appendRow(groupItem);
    }

    categoryTreeView->expandAll();
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
    QSettings settings("Techxartisan", "Openterface");
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            bool enabled = nameItem->checkState() == Qt::Checked;
            QString level = levelItem->text();
            settings.setValue(QString("log/category/%1/enabled").arg(category), enabled);
            settings.setValue(QString("log/category/%1/level").arg(category), level);
        }
    }
}

void LogPage::restoreCategorySettings()
{
    QSettings settings("Techxartisan", "Openterface");
    for (int g = 0; g < categoryModel->rowCount(); ++g) {
        QStandardItem *group = categoryModel->item(g);
        for (int c = 0; c < group->rowCount(); ++c) {
            QStandardItem *nameItem = group->child(c, 0);
            QStandardItem *levelItem = group->child(c, 1);
            QString category = nameItem->data(Qt::UserRole + 1).toString();
            QString defaultLevel = getDefaultLevel(category);
            bool enabled = settings.value(QString("log/category/%1/enabled").arg(category), true).toBool();
            QString level = settings.value(QString("log/category/%1/level").arg(category), defaultLevel).toString();
            nameItem->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
            levelItem->setText(level);
        }
    }
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
                qDebug() << "Created new log file:" << logPath;
            } else {
                qWarning() << "Failed to create log file:" << logPath;
            }
        }
        logFilePathLineEdit->setText(logPath);
    }
}

void LogPage::initLogSettings()
{
    qDebug() << "initLogSettings";
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
    qDebug() << "Applying log filter rules:" << logFilter;

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

    // Restore tree state
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
