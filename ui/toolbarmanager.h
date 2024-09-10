#ifndef TOOLBARMANAGER_H
#define TOOLBARMANAGER_H

#include <QObject>
#include <QToolBar>
#include <QPushButton>
#include <QComboBox>

class ToolbarManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolbarManager(QWidget *parent = nullptr);
    QToolBar* getToolbar() { return toolbar; }

private:
    QToolBar *toolbar;
    void setupToolbar();
    QPushButton* createFunctionButton(const QString &text);

signals:
    void functionKeyPressed(int key);
    void ctrlAltDelPressed();
    void delPressed();
    void repeatingKeystrokeChanged(int interval);

private slots:
    void onFunctionButtonClicked();
    void onCtrlAltDelClicked();
    void onDelClicked();
    void onRepeatingKeystrokeChanged(int index);
};

#endif // TOOLBARMANAGER_H