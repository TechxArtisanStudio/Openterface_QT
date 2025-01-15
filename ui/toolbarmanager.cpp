#include "toolbarmanager.h"
#include "../global.h"
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
        "}";

// Define constants for all special keys
const QString ToolbarManager::KEY_WIN = "Win";
const QString ToolbarManager::KEY_WIN_TOOLTIP = "Press Windows key.";

const QString ToolbarManager::KEY_PRTSC = "PrtSc";
const QString ToolbarManager::KEY_PRTSC_TOOLTIP = "Take a screenshot.";

const QString ToolbarManager::KEY_SCRLK = "ScrLk";
const QString ToolbarManager::KEY_SCRLK_TOOLTIP = "Toggle Scroll Lock.";

const QString ToolbarManager::KEY_PAUSE = "Pause";
const QString ToolbarManager::KEY_PAUSE_TOOLTIP = "Pause the system.";

const QString ToolbarManager::KEY_INS = "Ins";
const QString ToolbarManager::KEY_INS_TOOLTIP = "Toggle Insert mode.";

const QString ToolbarManager::KEY_HOME = "Home";
const QString ToolbarManager::KEY_HOME_TOOLTIP = "Move to the beginning of the line.";

const QString ToolbarManager::KEY_END = "End";
const QString ToolbarManager::KEY_END_TOOLTIP = "Move to the end of the line.";

const QString ToolbarManager::KEY_PGUP = "PgUp";
const QString ToolbarManager::KEY_PGUP_TOOLTIP = "Move up one page.";

const QString ToolbarManager::KEY_PGDN = "PgDn";
const QString ToolbarManager::KEY_PGDN_TOOLTIP = "Move down one page.";

const QString ToolbarManager::KEY_NUMLK = "NumLk";
const QString ToolbarManager::KEY_NUMLK_TOOLTIP = "Toggle Num Lock.";

const QString ToolbarManager::KEY_CAPSLK = "CapsLk";
const QString ToolbarManager::KEY_CAPSLK_TOOLTIP = "Toggle Caps Lock.";

const QString ToolbarManager::KEY_ESC = "Esc";
const QString ToolbarManager::KEY_ESC_TOOLTIP = "Cancel or exit current operation.";

const QString ToolbarManager::KEY_DEL = "Del";
const QString ToolbarManager::KEY_DEL_TOOLTIP = "Delete the character after the cursor.";

const QList<QPair<QString, QString>> ToolbarManager::specialKeys = {
    {KEY_WIN, KEY_WIN_TOOLTIP},
    {KEY_PRTSC, KEY_PRTSC_TOOLTIP},
    {KEY_SCRLK, KEY_SCRLK_TOOLTIP},
    {KEY_PAUSE, KEY_PAUSE_TOOLTIP},
    {KEY_INS, KEY_INS_TOOLTIP},
    {KEY_HOME, KEY_HOME_TOOLTIP},
    {KEY_END, KEY_END_TOOLTIP},
    {KEY_PGUP, KEY_PGUP_TOOLTIP},
    {KEY_PGDN, KEY_PGDN_TOOLTIP},
    {KEY_NUMLK, KEY_NUMLK_TOOLTIP},
    {KEY_CAPSLK, KEY_CAPSLK_TOOLTIP},
    {KEY_ESC, KEY_ESC_TOOLTIP},
    {KEY_DEL, KEY_DEL_TOOLTIP}
};

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

    // Add Ctrl+Alt+Del button first
    QPushButton *ctrlAltDelButton = new QPushButton("Ctrl+Alt+Del", toolbar);
    ctrlAltDelButton->setStyleSheet(commonButtonStyle);
    ctrlAltDelButton->setToolTip("Send Ctrl+Alt+Del keystroke.");
    connect(ctrlAltDelButton, &QPushButton::clicked, this, &ToolbarManager::onCtrlAltDelClicked);
    toolbar->addWidget(ctrlAltDelButton);

    // Add a spacer
    QWidget *spacer = new QWidget();
    spacer->setFixedWidth(10);
    toolbar->addWidget(spacer);

    // Function keys
    for (int i = 1; i <= 12; i++) {
        QString buttonText = QString("F%1").arg(i);
        QPushButton *button = createFunctionButton(buttonText);
        button->setToolTip(QString("Press Function key F%1.").arg(i));
        toolbar->addWidget(button);
    }

    // Add a spacer
    QWidget *spacer2 = new QWidget();
    spacer2->setFixedWidth(10);
    toolbar->addWidget(spacer2);

    // Special keys
    for (const auto &keyPair : specialKeys) {
        QPushButton *button = new QPushButton(keyPair.first, toolbar);
        button->setStyleSheet(commonButtonStyle);
        int width = button->fontMetrics().horizontalAdvance(keyPair.first) + 16; // Add some padding
        button->setFixedWidth(width);
        button->setToolTip(keyPair.second); // Set the tooltip
        connect(button, &QPushButton::clicked, this, &ToolbarManager::onSpecialKeyClicked);
        toolbar->addWidget(button);
    }
    
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

QPushButton* ToolbarManager::createFunctionButton(const QString &text)
{
    QPushButton *button = new QPushButton(text, toolbar);
    button->setStyleSheet(commonButtonStyle);
    button->setFixedWidth(40);
    connect(button, &QPushButton::clicked, this, &ToolbarManager::onFunctionButtonClicked);
    return button;
}

void ToolbarManager::onFunctionButtonClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        QString buttonText = button->text();
        int functionKeyNumber = buttonText.mid(1).toInt();
        int qtKeyCode = Qt::Key_F1 + functionKeyNumber - 1;
        HostManager::getInstance().handleFunctionKey(qtKeyCode);
    }
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

void ToolbarManager::onSpecialKeyClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        QString keyText = button->text();
        if (keyText == ToolbarManager::KEY_ESC) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Escape);
        } else if (keyText == ToolbarManager::KEY_INS) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Insert);
        } else if (keyText == ToolbarManager::KEY_DEL) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Delete);
        } else if (keyText == ToolbarManager::KEY_HOME) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Home);
        } else if (keyText == ToolbarManager::KEY_END) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_End);
        } else if (keyText == ToolbarManager::KEY_PGUP) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_PageUp);
        } else if (keyText == ToolbarManager::KEY_PGDN) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_PageDown);
        } else if (keyText == ToolbarManager::KEY_PRTSC) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Print);
        } else if (keyText == ToolbarManager::KEY_SCRLK) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_ScrollLock);
        } else if (keyText == ToolbarManager::KEY_PAUSE) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Pause);
        } else if (keyText == ToolbarManager::KEY_NUMLK) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_NumLock);
        } else if (keyText == ToolbarManager::KEY_CAPSLK) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_CapsLock);
        } else if (keyText == ToolbarManager::KEY_WIN) {
            HostManager::getInstance().handleFunctionKey(Qt::Key_Meta);
        }
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