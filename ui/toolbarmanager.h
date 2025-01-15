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


    // Define constants for all special keys
    static const QString KEY_WIN;
    static const QString KEY_WIN_TOOLTIP;
    static const QString KEY_PRTSC;
    static const QString KEY_PRTSC_TOOLTIP;
    static const QString KEY_SCRLK;
    static const QString KEY_SCRLK_TOOLTIP;
    static const QString KEY_PAUSE;
    static const QString KEY_PAUSE_TOOLTIP;
    static const QString KEY_INS;
    static const QString KEY_INS_TOOLTIP;
    static const QString KEY_HOME;
    static const QString KEY_HOME_TOOLTIP;
    static const QString KEY_END;
    static const QString KEY_END_TOOLTIP;
    static const QString KEY_PGUP;
    static const QString KEY_PGUP_TOOLTIP;
    static const QString KEY_PGDN;
    static const QString KEY_PGDN_TOOLTIP;
    static const QString KEY_NUMLK;
    static const QString KEY_NUMLK_TOOLTIP;
    static const QString KEY_CAPSLK;
    static const QString KEY_CAPSLK_TOOLTIP;
    static const QString KEY_ESC;
    static const QString KEY_ESC_TOOLTIP;
    static const QString KEY_DEL;
    static const QString KEY_DEL_TOOLTIP;
    static const QList<QPair<QString, QString>> specialKeys;

    // Add this line to declare the toggleToolbar function
    void toggleToolbar();
    void updateStyles();

signals:
    void toolbarVisibilityChanged(bool visible);

private:
    static const QString commonButtonStyle;

    QToolBar *toolbar;
    void setupToolbar();
    QPushButton* createFunctionButton(const QString &text);

private slots:
    void onFunctionButtonClicked();
    void onCtrlAltDelClicked();
    void onRepeatingKeystrokeChanged(int index);
    void onSpecialKeyClicked();

};

#endif // TOOLBARMANAGER_H