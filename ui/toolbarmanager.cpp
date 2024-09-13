#include "toolbarmanager.h"
#include "../global.h"
#include <QHBoxLayout>
#include <QWidget>

const QString ToolbarManager::commonButtonStyle = 
    "QPushButton { "
    "   border: 1px dotted rgba(0, 0, 0, 100); "
    "   background-color: rgba(240, 240, 240, 200); "
    "   padding: 2px; "
    "   margin: 2px; "
    "} "
    "QPushButton:pressed { "
    "   background-color: rgba(200, 200, 200, 200); "
    "   border: 1px solid rgba(0, 0, 0, 150); "
    "}";

// Define constants for all special keys
const QString ToolbarManager::KEY_WIN = "Win";
const QString ToolbarManager::KEY_PRTSC = "PrtSc";
const QString ToolbarManager::KEY_SCRLK = "ScrLk";
const QString ToolbarManager::KEY_PAUSE = "Pause";
const QString ToolbarManager::KEY_INS = "Ins";
const QString ToolbarManager::KEY_HOME = "Home";
const QString ToolbarManager::KEY_END = "End";
const QString ToolbarManager::KEY_PGUP = "PgUp";
const QString ToolbarManager::KEY_PGDN = "PgDn";
const QString ToolbarManager::KEY_NUMLK = "NumLk";
const QString ToolbarManager::KEY_CAPSLK = "CapsLk";
const QString ToolbarManager::KEY_ESC = "Esc";
const QString ToolbarManager::KEY_DEL = "Del";

const QStringList ToolbarManager::specialKeys = {
    KEY_WIN, KEY_PRTSC, KEY_SCRLK, KEY_PAUSE,
    KEY_INS, KEY_HOME, KEY_END, KEY_PGUP,
    KEY_PGDN, KEY_NUMLK, KEY_CAPSLK, KEY_ESC, KEY_DEL
};

ToolbarManager::ToolbarManager(QWidget *parent) : QObject(parent)
{
    setupToolbar();
}

void ToolbarManager::setupToolbar()
{
    toolbar = new QToolBar(qobject_cast<QWidget*>(parent()));
    toolbar->setStyleSheet("QToolBar { background-color: rgba(200, 200, 200, 150); border: none; }");
    toolbar->setFloatable(false);
    toolbar->setMovable(false);

    // Function keys
    for (int i = 1; i <= 12; i++) {
        QString buttonText = QString("F%1").arg(i);
        QPushButton *button = createFunctionButton(buttonText);
        toolbar->addWidget(button);

    }

    // Add a spacer
    QWidget *spacer = new QWidget();
    spacer->setFixedWidth(10);
    toolbar->addWidget(spacer);

    // Special keys
    for (const QString &keyText : specialKeys) {
        QPushButton *button = new QPushButton(keyText, toolbar);
        button->setStyleSheet(commonButtonStyle);
        int width = button->fontMetrics().horizontalAdvance(keyText) + 16; // Add some padding
        button->setFixedWidth(width);
        connect(button, &QPushButton::clicked, this, &ToolbarManager::onSpecialKeyClicked);
        toolbar->addWidget(button);
    }

    // Existing special buttons
    QPushButton *ctrlAltDelButton = new QPushButton("Ctrl+Alt+Del", toolbar);
    ctrlAltDelButton->setStyleSheet(commonButtonStyle);
    int ctrlAltDelWidth = ctrlAltDelButton->fontMetrics().horizontalAdvance("Ctrl+Alt+Del") + 16;
    ctrlAltDelButton->setFixedWidth(ctrlAltDelWidth);
    connect(ctrlAltDelButton, &QPushButton::clicked, this, &ToolbarManager::onCtrlAltDelClicked);
    toolbar->addWidget(ctrlAltDelButton);

    // QPushButton *delButton = new QPushButton("Del", toolbar);
    // delButton->setStyleSheet(commonButtonStyle);
    // connect(delButton, &QPushButton::clicked, this, &ToolbarManager::onDelClicked);
    // toolbar->addWidget(delButton);

    // Repeating keystroke combo box

    QComboBox *repeatingKeystrokeComboBox = new QComboBox(toolbar);
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
    int width = button->fontMetrics().horizontalAdvance(text) + 16; // Add some padding
    button->setFixedWidth(width);

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
        emit functionKeyPressed(qtKeyCode);
    }
}

void ToolbarManager::onCtrlAltDelClicked()
{
    emit ctrlAltDelPressed();
}

void ToolbarManager::onRepeatingKeystrokeChanged(int index)
{
    QComboBox *comboBox = qobject_cast<QComboBox*>(sender());
    if (comboBox) {
        int interval = comboBox->itemData(index).toInt();
        emit repeatingKeystrokeChanged(interval);
    }
}

void ToolbarManager::onSpecialKeyClicked()
{
    QPushButton *button = qobject_cast<QPushButton*>(sender());
    if (button) {
        QString keyText = button->text();
        emit specialKeyPressed(keyText);
    }
}

void ToolbarManager::toggleToolbar() {
    if (toolbar->isVisible()) {
        toolbar->hide();
        GlobalVar::instance().setToolbarVisible(false);
    } else {
        toolbar->show();
        GlobalVar::instance().setToolbarVisible(true);
        GlobalVar::instance().setToolbarHeight(toolbar->height());
    }
}