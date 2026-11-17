#include "toolbarmanager.h"
#include "global.h"
#include "../globalsetting.h"
#include "host/HostManager.h"
#include "../customkey/customkeymanager.h"
#include "../../target/KeyboardLayouts.h"
#include "log/opflogging.h"
#include <QHBoxLayout>
#include <QWidget>
#include <QToolButton>
#include <QStyle>
#include <QTimer>
#include <QPropertyAnimation>
#include <QSettings>

OPF_LOGGING_CATEGORY(log_toolbar_keyboard, "opf.ui.keyboard")

const QString ToolbarManager::commonButtonStyle =
        "QPushButton { "
        "   border: 1px solid palette(mid); "
        "   background-color: palette(button); "
        "   color: palette(buttonText); "
        "   padding: 2px; "
        "   margin: 2px; "
        "} "
        "QPushButton:pressed { "
        "   background-color: palette(dark); "
        "   border: 1px solid palette(shadow); "
        "} "
        "QPushButton[openterface_modifier] { "
        "   color: palette(highlight); "
        "} "
        "QPushButton[openterface_modifier]:checked { "
        "   background-color: palette(dark); "
        "}";

const char *ToolbarManager::KEYCODE_PROPERTY = "openterface_keyCode";
const char *ToolbarManager::MODIFIER_PROPERTY = "openterface_modifier";

const QList<ToolbarManager::ModifierInfo> ToolbarManager::modifierButtons = {
    {"Ctrl", "Toggle Ctrl modifier.", Qt::ControlModifier},
    {"Alt", "Toggle Alt modifier.", Qt::AltModifier},
    {"Shift", "Toggle Shift modifier.", Qt::ShiftModifier},
    {"Win", "Toggle Windows modifier.", Qt::MetaModifier},
};

ToolbarManager::ToolbarManager(QWidget *parent) : QObject(parent)
{
    // Load saved keyboard layout preference from GlobalSetting (same as corner widget)
    GlobalSetting::instance().getKeyboardLayout(m_currentLayout);

    setupToolbar();
}

void ToolbarManager::setupToolbar()
{
    toolbar = new QToolBar(qobject_cast<QWidget*>(parent()));
    toolbar->setStyleSheet("QToolBar { background-color: palette(window); border: none; }");
    toolbar->setFloatable(false);
    toolbar->setMovable(false);

    // Initialize CustomKeyManager and load keys
    CustomKeyManager& keyManager = CustomKeyManager::getInstance();
    keyManager.initialize();

    // Build toolbar (modifier buttons + custom keys)
    rebuildToolbar();

    // Add config button
    toolbar->addSeparator();
    QPushButton *configButton = new QPushButton(tr("⚙"), toolbar);
    configButton->setToolTip(tr("Custom Key Configuration"));
    configButton->setStyleSheet(commonButtonStyle);
    configButton->setFixedWidth(40);
    toolbar->addWidget(configButton);
    connect(configButton, &QPushButton::clicked, this, &ToolbarManager::onCustomKeyButtonClicked);

    toolbar->addSeparator();

    // Add keyboard layout selector
    m_layoutComboBox = new QComboBox(toolbar);
    m_layoutComboBox->setToolTip(tr("Select Keyboard Layout"));
    m_layoutComboBox->setStyleSheet(
        "QComboBox { "
        "   border: 1px solid palette(mid); "
        "   background-color: palette(button); "
        "   color: palette(buttonText); "
        "   padding: 2px; "
        "   margin: 2px; "
        "   min-width: 100px; "
        "} "
        "QComboBox QAbstractItemView { "
        "   background-color: palette(base); "
        "   color: palette(text); "
        "}"
    );
    toolbar->addWidget(m_layoutComboBox);
    connect(m_layoutComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolbarManager::onLayoutChanged);

    // Repeating keystroke combo box
    QComboBox *repeatingKeystrokeComboBox = new QComboBox(toolbar);
    repeatingKeystrokeComboBox->setStyleSheet(
        "QComboBox { "
        "   border: 1px solid palette(mid); "
        "   background-color: palette(button); "
        "   color: palette(text); "
        "   padding: 2px; "
        "   margin: 2px; "
        "} "
        "QComboBox QAbstractItemView { "
        "   background-color: palette(base); "
        "   color: palette(text); "
        "}"
    );
    repeatingKeystrokeComboBox->setToolTip(tr("Set keystroke repeat interval."));
    repeatingKeystrokeComboBox->addItem(tr("No repeating"), 0);
    repeatingKeystrokeComboBox->addItem(tr("Repeat every 0.5s"), 500);
    repeatingKeystrokeComboBox->addItem(tr("Repeat every 1s"), 1000);
    repeatingKeystrokeComboBox->addItem(tr("Repeat every 2s"), 2000);
    toolbar->addWidget(repeatingKeystrokeComboBox);

    connect(repeatingKeystrokeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolbarManager::onRepeatingKeystrokeChanged);

    // Populate keyboard layout selector (already created in rebuildToolbar)
    populateLayoutSelector();
}

void ToolbarManager::rebuildToolbar()
{
    // Remove custom key actions, but preserve modifier buttons and other fixed widgets
    // We rebuild by clearing everything and re-adding in the correct order

    // Remove all actions first, deleting any associated widgets
    QList<QAction*> existingActions = toolbar->actions();
    for (QAction* a : existingActions) {
        QWidget *w = toolbar->widgetForAction(a);
        if (w) w->deleteLater();
        toolbar->removeAction(a);
    }

    // Add virtual keyboard button first
    QPushButton *virtualKbButton = new QPushButton(toolbar);
    virtualKbButton->setToolTip(tr("Virtual Keyboard"));
    virtualKbButton->setIcon(QIcon(":/images/keyboard.svg"));
    virtualKbButton->setIconSize(QSize(24, 24));
    virtualKbButton->setStyleSheet(commonButtonStyle);
    virtualKbButton->setFixedWidth(45);
    virtualKbButton->setFixedHeight(32);
    toolbar->addWidget(virtualKbButton);
    connect(virtualKbButton, &QPushButton::clicked, this, &ToolbarManager::onVirtualKeyboardClicked);

    toolbar->addSeparator();

    // Re-add modifier toggle buttons first
    for (const auto& modInfo : modifierButtons) {
        QPushButton *button = addKeyButton(modInfo.text, modInfo.toolTip);
        button->setCheckable(true);
        button->setProperty(MODIFIER_PROPERTY, modInfo.modifierFlag);
    }
    toolbar->addSeparator();

    CustomKeyManager& keyManager = CustomKeyManager::getInstance();
    QList<CustomKeyInfo> keys = keyManager.getKeys();

    for (const CustomKeyInfo& info : keys) {
        if (info.isSeparator) {
            toolbar->addSeparator();
            continue;
        }

        QPushButton *button = addKeyButton(info.displayName, info.displayName);

        if (!info.specialCombo.isEmpty() && info.specialCombo == "ctrl_alt_del") {
            connect(button, &QPushButton::clicked, this, &ToolbarManager::onCtrlAltDelClicked);
        } else if (!info.keyCodes.isEmpty()) {
            // Store keyCodes for custom key buttons
            QVariant keyCodesVar = QVariant::fromValue(info.keyCodes);
            button->setProperty("customkey_keyCodes", keyCodesVar);
            connect(button, &QPushButton::clicked, this, &ToolbarManager::onKeyButtonClicked);
        } else {
            // No keyCodes - button exists but does nothing until configured
            button->setToolTip(tr("Double-click to configure in Custom Key Settings"));
        }
    }
}

void ToolbarManager::onCustomKeyButtonClicked()
{
    emit openCustomKeyConfig();
}

void ToolbarManager::onVirtualKeyboardClicked()
{
    emit openVirtualKeyboard();
}

QPushButton *ToolbarManager::addKeyButton(const QString& text, const QString& toolTip)
{
    QPushButton *button = new QPushButton(text, toolbar);
    button->setStyleSheet(commonButtonStyle);
    int width = button->fontMetrics().horizontalAdvance(text) + 16;
    button->setFixedWidth(std::max(width, 40));
    button->setToolTip(toolTip);
    button->setFocusPolicy(Qt::TabFocus);
    toolbar->addWidget(button);
    return button;
}

void ToolbarManager::onKeyButtonClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (!button) {
        return;
    }

    // Collect modifier flags from checked modifier buttons
    int modifiers = 0;
    for (QPushButton *btn : toolbar->findChildren<QPushButton*>()) {
        int modifier = btn->property(MODIFIER_PROPERTY).toInt();
        if (modifier != 0 && btn->isChecked()) {
            modifiers |= modifier;
            btn->setChecked(false);  // Auto-uncheck after collecting
        }
    }

    // Check if this is a custom key with keyCodes
    QVariant keyCodesVar = button->property("customkey_keyCodes");
    if (keyCodesVar.isValid()) {
        QList<int> keyCodes = keyCodesVar.value<QList<int>>();
        if (!keyCodes.isEmpty()) {
            // If modifiers are toggled, prepend them to the keyCodes
            if (modifiers != 0) {
                QList<int> combinedKeyCodes;
                if (modifiers & Qt::ControlModifier) combinedKeyCodes << Qt::Key_Control;
                if (modifiers & Qt::ShiftModifier) combinedKeyCodes << Qt::Key_Shift;
                if (modifiers & Qt::AltModifier) combinedKeyCodes << Qt::Key_Alt;
                if (modifiers & Qt::MetaModifier) combinedKeyCodes << Qt::Key_Meta;
                combinedKeyCodes.append(keyCodes);
                HostManager::getInstance().handleKeyCombo(combinedKeyCodes);
            } else {
                HostManager::getInstance().handleKeyCombo(keyCodes);
            }
        }
        return;
    }

    // Fallback to legacy behavior (single key)
    int keyCode = button->property(KEYCODE_PROPERTY).toInt();
    if (keyCode == 0) {
        return;
    }

    // Merge with any physical keyboard modifiers
    modifiers |= QGuiApplication::keyboardModifiers();
    HostManager::getInstance().handleFunctionKey(keyCode, modifiers);
}

void ToolbarManager::onCtrlAltDelClicked()
{
    HostManager::getInstance().sendCtrlAltDel();
}

void ToolbarManager::onRepeatingKeystrokeChanged(int index)
{
    QComboBox *comboBox = qobject_cast<QComboBox*>(sender());
    if (comboBox) {
        int interval = comboBox->itemData(index).toInt();
        HostManager::getInstance().setRepeatingKeystroke(interval);
    }
}

void ToolbarManager::toggleToolbar() {

    if (!toolbar) {
        qWarning() << "ToolbarManager::toggleToolbar() - toolbar is null!";
        return;
    }

    QList<QPropertyAnimation*> animations = toolbar->findChildren<QPropertyAnimation*>();
    for (QPropertyAnimation *anim : animations) {
        if (anim->targetObject() == toolbar && anim->propertyName() == "maximumHeight") {
            anim->stop();
            anim->deleteLater();
        }
    }

    QPropertyAnimation *animation = new QPropertyAnimation(toolbar, "maximumHeight");
    animation->setDuration(150);

    if (toolbar->isVisible()) {
        int startHeight = toolbar->height();
        animation->setStartValue(startHeight);
        animation->setEndValue(0);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            if (toolbar) {
                toolbar->hide();
                GlobalVar::instance().setToolbarVisible(false);
                emit toolbarVisibilityChanged(false);
            } else {
                qWarning() << "ToolbarManager::toggleToolbar() - Toolbar became null during hide animation!";
            }
        });
    } else {
        int targetHeight = toolbar->sizeHint().height();
        toolbar->show();
        animation->setStartValue(0);
        animation->setEndValue(targetHeight);
        GlobalVar::instance().setToolbarVisible(true);
        GlobalVar::instance().setToolbarHeight(targetHeight);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            if (toolbar) {
                emit toolbarVisibilityChanged(true);
            } else {
                qWarning() << "ToolbarManager::toggleToolbar() - Toolbar became null during show animation!";
            }
        });
    }

    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToolbarManager::updateStyles()
{
    toolbar->setStyleSheet("QToolBar { background-color: palette(window); border: none; }");

    for (QAction *action : toolbar->actions()) {
        QWidget *widget = toolbar->widgetForAction(action);
        if (QPushButton *button = qobject_cast<QPushButton*>(widget)) {
            button->setStyleSheet(commonButtonStyle);
        } else if (QComboBox *comboBox = qobject_cast<QComboBox*>(widget)) {
            comboBox->setStyleSheet(
                "QComboBox { "
                "   border: 1px solid palette(mid); "
                "   background-color: palette(button); "
                "   color: palette(buttonText); "
                "   padding: 2px; "
                "   margin: 2px; "
                "} "
                "QComboBox QAbstractItemView { "
                "   background-color: palette(base); "
                "   color: palette(text); "
                "}"
            );
        } else if (QToolButton *toolButton = qobject_cast<QToolButton*>(widget)) {
            toolButton->setStyleSheet(
                "QToolButton { "
                "   border: 1px solid palette(mid); "
                "   background-color: palette(button); "
                "   color: palette(buttonText); "
                "   padding: 2px; "
                "   margin: 2px; "
                "} "
                "QToolButton::menu-indicator { image: none; }"
            );
        }
    }
}

void ToolbarManager::populateLayoutSelector()
{
    if (!m_layoutComboBox) return;

    // Get available layouts from KeyboardLayoutManager
    QStringList layouts = KeyboardLayoutManager::getInstance().getAvailableLayouts();

    // Clear existing items
    m_layoutComboBox->clear();

    // Add layouts to combo box
    for (const QString& layout : layouts) {
        m_layoutComboBox->addItem(layout, layout);
    }

    // Set to the saved layout from GlobalSetting if available, otherwise use US QWERTY
    if (!m_currentLayout.isEmpty()) {
        int savedIndex = m_layoutComboBox->findText(m_currentLayout);
        if (savedIndex >= 0) {
            m_layoutComboBox->setCurrentIndex(savedIndex);
        } else {
            // Saved layout not found, fall back to US QWERTY
            int defaultIndex = m_layoutComboBox->findText("US QWERTY");
            if (defaultIndex >= 0) {
                m_layoutComboBox->setCurrentIndex(defaultIndex);
                m_currentLayout = "US QWERTY";
            }
        }
    } else {
        // No saved layout, default to US QWERTY
        int defaultIndex = m_layoutComboBox->findText("US QWERTY");
        if (defaultIndex >= 0) {
            m_layoutComboBox->setCurrentIndex(defaultIndex);
            m_currentLayout = "US QWERTY";
        }
    }
}

void ToolbarManager::onLayoutChanged(int index)
{
    if (!m_layoutComboBox) return;

    QString layoutName = m_layoutComboBox->itemText(index);
    qCInfo(log_toolbar_keyboard) << "Layout changed to:" << layoutName << "current:" << m_currentLayout;
    if (!layoutName.isEmpty() && layoutName != m_currentLayout) {
        m_currentLayout = layoutName;

        // Save the layout preference using GlobalSetting (same as corner widget)
        GlobalSetting::instance().setKeyboardLayout(m_currentLayout);

        qCInfo(log_toolbar_keyboard) << "Emitting layoutChanged signal:" << layoutName;
        emit layoutChanged(layoutName);
    }
}

void ToolbarManager::setCurrentLayout(const QString& layoutName)
{
    if (layoutName == m_currentLayout) return;

    qCInfo(log_toolbar_keyboard) << "Setting toolbar layout to:" << layoutName;
    m_currentLayout = layoutName;

    // Update the combo box selection without emitting signal (to avoid circular updates)
    if (m_layoutComboBox) {
        m_layoutComboBox->blockSignals(true);
        int index = m_layoutComboBox->findText(layoutName);
        if (index >= 0) {
            m_layoutComboBox->setCurrentIndex(index);
        }
        m_layoutComboBox->blockSignals(false);
    }

    // Save the layout preference using GlobalSetting (same as corner widget)
    GlobalSetting::instance().setKeyboardLayout(m_currentLayout);
}
