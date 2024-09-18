#include "toolbarmanager.h"
#include <QHBoxLayout>
#include <QWidget>

ToolbarManager::ToolbarManager(QWidget *parent) : QObject(parent)
{
    setupToolbar();
}

void ToolbarManager::setupToolbar()
{
    toolbar = new QToolBar(qobject_cast<QWidget*>(parent()));
    toolbar->setStyleSheet("QToolBar { background-color: rgba(50, 50, 50, 255); border: none; }"); // Darker background for better contrast
    toolbar->setFloatable(false);
    toolbar->setMovable(false);

    for (int i = 1; i <= 12; i++) {
        QString buttonText = QString("F%1").arg(i);
        QPushButton *button = createFunctionButton(buttonText);
        button->setToolTip(QString("Press Function key F%1".).arg(i));
        toolbar->addWidget(button);

        if (i % 4 == 0 && i < 12) {
            QWidget *smallSpacer = new QWidget();
            smallSpacer->setFixedWidth(10);
            toolbar->addWidget(smallSpacer);
        }
    }

    QPushButton *ctrlAltDelButton = new QPushButton("Ctrl+Alt+Del", toolbar);
    ctrlAltDelButton->setStyleSheet(
        "QPushButton { "
        "   border: 1px solid rgba(255, 255, 255, 150); " // Lighter border for better contrast
        "   background-color: rgba(100, 100, 100, 255); " // Darker background for better contrast
        "   color: white; " // White text for better contrast
        "   padding: 2px; "
        "   margin: 2px; "
        "} "
        "QPushButton:pressed { "
        "   background-color: rgba(80, 80, 80, 255); " // Darker background for better contrast
        "   border: 1px solid rgba(255, 255, 255, 200); " // Lighter border for better contrast
        "}"
    );
    ctrlAltDelButton->setToolTip("Send Ctrl+Alt+Del keystroke.");
    connect(ctrlAltDelButton, &QPushButton::clicked, this, &ToolbarManager::onCtrlAltDelClicked);
    toolbar->addWidget(ctrlAltDelButton);

    QPushButton *delButton = new QPushButton("Del", toolbar);
    delButton->setStyleSheet(ctrlAltDelButton->styleSheet());
    delButton->setToolTip("Send Delete keystroke.");
    connect(delButton, &QPushButton::clicked, this, &ToolbarManager::onDelClicked);
    toolbar->addWidget(delButton);

    QComboBox *repeatingKeystrokeComboBox = new QComboBox(toolbar);
    repeatingKeystrokeComboBox->setStyleSheet(
        "QComboBox { "
        "   border: 1px solid rgba(255, 255, 255, 150); " // Lighter border for better contrast
        "   background-color: rgba(100, 100, 100, 255); " // Darker background for better contrast
        "   color: white; " // White text for better contrast
        "   padding: 2px; "
        "   margin: 2px; "
        "} "
        "QComboBox QAbstractItemView { "
        "   background-color: rgba(100, 100, 100, 255); " // Darker background for better contrast
        "   color: white; " // White text for better contrast
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
    button->setStyleSheet(
        "QPushButton { "
        "   border: 1px solid rgba(255, 255, 255, 150); " // Lighter border for better contrast
        "   background-color: rgba(100, 100, 100, 255); " // Darker background for better contrast
        "   color: white; " // White text for better contrast
        "   padding: 2px; "
        "   margin: 2px; "
        "} "
        "QPushButton:pressed { "
        "   background-color: rgba(80, 80, 80, 255); " // Darker background for better contrast
        "   border: 1px solid rgba(255, 255, 255, 200); " // Lighter border for better contrast
        "}"
    );
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
        emit functionKeyPressed(qtKeyCode);
    }
}

void ToolbarManager::onCtrlAltDelClicked()
{
    emit ctrlAltDelPressed();
}

void ToolbarManager::onDelClicked()
{
    emit delPressed();
}

void ToolbarManager::onRepeatingKeystrokeChanged(int index)
{
    QComboBox *comboBox = qobject_cast<QComboBox*>(sender());
    if (comboBox) {
        int interval = comboBox->itemData(index).toInt();
        emit repeatingKeystrokeChanged(interval);
    }
}