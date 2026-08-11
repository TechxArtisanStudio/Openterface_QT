#ifndef TOOLBARMANAGER_H
#define TOOLBARMANAGER_H

#include <QObject>
#include <QToolBar>
#include <QPushButton>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QStringList>

class ToolbarManager : public QObject
{
    Q_OBJECT

public:
    explicit ToolbarManager(QWidget *parent = nullptr);
    QToolBar* getToolbar() { return toolbar; }
    void toggleToolbar();
    void updateStyles();

signals:
    void toolbarVisibilityChanged(bool visible);
    void openCustomKeyConfig();

private:
    struct KeyInfo {
        QString text;
        QString toolTip;
        int keyCode;
    };

    static const QString commonButtonStyle;
    static const QList<KeyInfo> modifierKeys;
    static const QList<KeyInfo> specialKeys;
    static const char *KEYCODE_PROPERTY;
    static const char *MODIFIER_PROPERTY;

    QToolBar *toolbar;
    void setupToolbar();
    void rebuildToolbar();
    QPushButton *addKeyButton(const QString& text, const QString& toolTip);
    void onCustomKeyButtonClicked();

private slots:
    void onKeyButtonClicked();
    void onCtrlAltDelClicked();
    void onRepeatingKeystrokeChanged(int index);

};

#endif // TOOLBARMANAGER_H
