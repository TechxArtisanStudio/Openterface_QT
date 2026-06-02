#include "toolbarmanager.h"
#include "global.h"
#include "host/HostManager.h"
#include "../customkey/customkeymanager.h"
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

    // Initialize CustomKeyManager and load keys
    CustomKeyManager& keyManager = CustomKeyManager::getInstance();
    keyManager.initialize();

    // Build toolbar from custom keys
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
}

void ToolbarManager::rebuildToolbar()
{
    // Remove all actions first, deleting any associated widgets
    QList<QAction*> existingActions = toolbar->actions();
    for (QAction* a : existingActions) {
        QWidget *w = toolbar->widgetForAction(a);
        if (w) w->deleteLater();
        toolbar->removeAction(a);
    }

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

    // Check if this is a custom key with keyCodes
    QVariant keyCodesVar = button->property("customkey_keyCodes");
    if (keyCodesVar.isValid()) {
        QList<int> keyCodes = keyCodesVar.value<QList<int>>();
        if (!keyCodes.isEmpty()) {
            HostManager::getInstance().handleKeyCombo(keyCodes);
        }
        return;
    }

    // Fallback to legacy behavior (single key)
    int keyCode = button->property(KEYCODE_PROPERTY).toInt();
    if (keyCode == 0) {
        return;
    }

    int modifiers = QGuiApplication::keyboardModifiers();
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
    qDebug() << "ToolbarManager::toggleToolbar() - Start";

    if (!toolbar) {
        qWarning() << "ToolbarManager::toggleToolbar() - toolbar is null!";
        return;
    }

    qDebug() << "ToolbarManager::toggleToolbar() - Current visibility:" << toolbar->isVisible()
             << "Height:" << toolbar->height() << "MaxHeight:" << toolbar->maximumHeight();

    QList<QPropertyAnimation*> animations = toolbar->findChildren<QPropertyAnimation*>();
    qDebug() << "ToolbarManager::toggleToolbar() - Found" << animations.size() << "existing animations";
    for (QPropertyAnimation *anim : animations) {
        if (anim->targetObject() == toolbar && anim->propertyName() == "maximumHeight") {
            qDebug() << "ToolbarManager::toggleToolbar() - Stopping existing maximumHeight animation";
            anim->stop();
            anim->deleteLater();
        }
    }

    qDebug() << "ToolbarManager::toggleToolbar() - Creating new animation";
    QPropertyAnimation *animation = new QPropertyAnimation(toolbar, "maximumHeight");
    animation->setDuration(150);

    if (toolbar->isVisible()) {
        int startHeight = toolbar->height();
        qDebug() << "ToolbarManager::toggleToolbar() - Hiding toolbar, animating from" << startHeight << "to 0";
        animation->setStartValue(startHeight);
        animation->setEndValue(0);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            qDebug() << "ToolbarManager::toggleToolbar() - Hide animation finished";
            if (toolbar) {
                toolbar->hide();
                GlobalVar::instance().setToolbarVisible(false);
                emit toolbarVisibilityChanged(false);
                qDebug() << "ToolbarManager::toggleToolbar() - Toolbar hidden successfully";
            } else {
                qWarning() << "ToolbarManager::toggleToolbar() - Toolbar became null during hide animation!";
            }
        });
    } else {
        int targetHeight = toolbar->sizeHint().height();
        qDebug() << "ToolbarManager::toggleToolbar() - Showing toolbar, animating from 0 to" << targetHeight;
        toolbar->show();
        animation->setStartValue(0);
        animation->setEndValue(targetHeight);
        GlobalVar::instance().setToolbarVisible(true);
        GlobalVar::instance().setToolbarHeight(targetHeight);
        connect(animation, &QPropertyAnimation::finished, this, [this]() {
            qDebug() << "ToolbarManager::toggleToolbar() - Show animation finished";
            if (toolbar) {
                emit toolbarVisibilityChanged(true);
                qDebug() << "ToolbarManager::toggleToolbar() - Toolbar shown successfully";
            } else {
                qWarning() << "ToolbarManager::toggleToolbar() - Toolbar became null during show animation!";
            }
        });
    }

    qDebug() << "ToolbarManager::toggleToolbar() - Starting animation";
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
