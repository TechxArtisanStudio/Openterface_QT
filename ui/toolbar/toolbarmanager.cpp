#include "toolbarmanager.h"
#include "global.h"
#include "host/HostManager.h"
#include <QHBoxLayout>
#include <QWidget>
#include <QToolButton>
#include <QStyle>
#include <QTimer>
#include <QPropertyAnimation>

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
        "}"
        "QPushButton[openterface_modifier] { "
        "   color: palette(highlight); "
        "}"
        "QPushButton[openterface_modifier]:checked { "
        "   background-color: palette(dark); "
        "}";

const QList<ToolbarManager::KeyInfo> ToolbarManager::modifierKeys = {
    {"Shift", "Toggle Shift modifier.", Qt::ShiftModifier},
    {"Ctrl", "Toggle Ctrl modifier.", Qt::ControlModifier},
    {"Alt", "Toggle Alt modifier.", Qt::AltModifier},
    {"Win", "Toggle Windows modifier.", Qt::MetaModifier},
};

const QList<ToolbarManager::KeyInfo> ToolbarManager::specialKeys = {
    {"Win", "Press Windows key.", Qt::Key_Meta},
    {"Esc", "Cancel or exit current operation.", Qt::Key_Escape},
    {"PrtSc", "Take a screenshot.", Qt::Key_Print},
    {"ScrLk", "Toggle Scroll Lock.", Qt::Key_ScrollLock},
    {"NumLk", "Toggle Num Lock.", Qt::Key_NumLock},
    {"CapsLk", "Toggle Caps Lock.", Qt::Key_CapsLock},
    {"Pause", "Pause the system.", Qt::Key_Pause},
    {"Ins", "Toggle Insert mode.", Qt::Key_Insert},
    {"Del", "Delete the character after the cursor.", Qt::Key_Delete},
    {"Home", "Move to the beginning of the line.", Qt::Key_Home},
    {"End", "Move to the end of the line.", Qt::Key_End},
    {"PgUp", "Move up one page.", Qt::Key_PageUp},
    {"PgDn", "Move down one page.", Qt::Key_PageDown},
};

const char *ToolbarManager::KEYCODE_PROPERTY = "openterface_keyCode";
const char *ToolbarManager::MODIFIER_PROPERTY = "openterface_modifier";

ToolbarManager::ToolbarManager(QWidget *parent) : QObject(parent)
{
    setupToolbar();
}

void ToolbarManager::setupToolbar()
{
    toolbar = new QToolBar(qobject_cast<QWidget*>(parent()));
    toolbar->setStyleSheet("QToolBar { background-color: palette(window); border: none; }");
    toolbar->setFloatable(false);
    toolbar->setMovable(false);

    // Modifier keys
    for (const auto& keyInfo : modifierKeys) {
        QPushButton *button = addKeyButton(keyInfo.text, keyInfo.toolTip);
        button->setCheckable(true);
        button->setProperty(MODIFIER_PROPERTY, keyInfo.keyCode);
    }

    toolbar->addSeparator();

    // Add Ctrl+Alt+Del button
    QPushButton *ctrlAltDelButton = addKeyButton("Ctrl+Alt+Del", "Send Ctrl+Alt+Del keystroke.");
    connect(ctrlAltDelButton, &QPushButton::clicked, this, &ToolbarManager::onCtrlAltDelClicked);

    toolbar->addSeparator();

    // Function keys
    for (int i = 1; i <= 12; i++) {
        QPushButton *button = addKeyButton(QString("F%1").arg(i), QString("Press Function key F%1.").arg(i));
        button->setProperty(KEYCODE_PROPERTY, Qt::Key_F1 + i - 1);
        connect(button, &QPushButton::clicked, this, &ToolbarManager::onKeyButtonClicked);
    }

    toolbar->addSeparator();

    // Special keys
    for (const auto &keyInfo : specialKeys) {
        QPushButton *button = addKeyButton(keyInfo.text, keyInfo.toolTip);
        button->setProperty(KEYCODE_PROPERTY, keyInfo.keyCode);
        connect(button, &QPushButton::clicked, this, &ToolbarManager::onKeyButtonClicked);
    }

    toolbar->addSeparator();

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
    repeatingKeystrokeComboBox->setToolTip("Set keystroke repeat interval.");
    repeatingKeystrokeComboBox->addItem("No repeating", 0);
    repeatingKeystrokeComboBox->addItem("Repeat every 0.5s", 500);
    repeatingKeystrokeComboBox->addItem("Repeat every 1s", 1000);
    repeatingKeystrokeComboBox->addItem("Repeat every 2s", 2000);
    toolbar->addWidget(repeatingKeystrokeComboBox);

    connect(repeatingKeystrokeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolbarManager::onRepeatingKeystrokeChanged);
}

QPushButton *ToolbarManager::addKeyButton(const QString& text, const QString& toolTip)
{
    QPushButton *button = new QPushButton(text, toolbar);
    button->setStyleSheet(commonButtonStyle);
    int width = button->fontMetrics().horizontalAdvance(text) + 16; // Add some padding
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

    int keyCode = button->property(KEYCODE_PROPERTY).toInt();
    if (keyCode == 0) {
        return;
    }

    int modifiers = QGuiApplication::keyboardModifiers();

    for (const auto& button : toolbar->findChildren<QPushButton*>()) {
        int modifier = button->property(MODIFIER_PROPERTY).toInt();
        if (modifier != 0 && button->isChecked()) {
            button->setChecked(false);
            modifiers |= modifier;
        }
    }

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
    // Prevent animation during visibility change
    toolbar->setStyleSheet("QToolBar { background-color: palette(window); border: none; animation-duration: 0; }");
    
    // Use QPropertyAnimation for smooth transition
    QPropertyAnimation *animation = new QPropertyAnimation(toolbar, "maximumHeight");
    animation->setDuration(150); // Adjust duration as needed
    
    if (toolbar->isVisible()) {
        animation->setStartValue(toolbar->height());
        animation->setEndValue(0);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            toolbar->hide();
            GlobalVar::instance().setToolbarVisible(false);
            emit toolbarVisibilityChanged(false);
        });
    } else {
        toolbar->show();
        animation->setStartValue(0);
        animation->setEndValue(toolbar->sizeHint().height());
        GlobalVar::instance().setToolbarVisible(true);
        GlobalVar::instance().setToolbarHeight(toolbar->sizeHint().height());
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            emit toolbarVisibilityChanged(true);
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