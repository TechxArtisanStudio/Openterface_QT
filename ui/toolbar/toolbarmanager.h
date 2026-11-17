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
    void rebuildToolbar();
    QString getCurrentLayout() const { return m_currentLayout; }
    void setCurrentLayout(const QString& layoutName);

signals:
    void toolbarVisibilityChanged(bool visible);
    void openCustomKeyConfig();
    void openVirtualKeyboard();
    void layoutChanged(const QString& layoutName);

private:
    struct KeyInfo {
        QString text;
        QString toolTip;
        int keyCode;
    };

    struct ModifierInfo {
        QString text;
        QString toolTip;
        int modifierFlag;
    };

    static const QString commonButtonStyle;
    static const QList<ModifierInfo> modifierButtons;
    static const QList<KeyInfo> specialKeys;
    static const char *KEYCODE_PROPERTY;
    static const char *MODIFIER_PROPERTY;

    QToolBar *toolbar;
    QComboBox *m_layoutComboBox;
    QString m_currentLayout;
    void setupToolbar();
    void populateLayoutSelector();
    QPushButton *addKeyButton(const QString& text, const QString& toolTip);
    void onCustomKeyButtonClicked();

private slots:
    void onKeyButtonClicked();
    void onCtrlAltDelClicked();
    void onRepeatingKeystrokeChanged(int index);
    void onVirtualKeyboardClicked();
    void onLayoutChanged(int index);

};

#endif // TOOLBARMANAGER_H
