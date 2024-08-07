#ifndef SETTINGDIALOG_H
#define SETTINGDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>


namespace Ui {
class SettingDialog;
}

class SettingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingDialog(QWidget *parent = nullptr);
    ~SettingDialog();

private:
    Ui::SettingDialog *ui;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;
    QWidget *buttonWidget;

    void switchWidgetShow(QString &btnName);
    void createSettingTree();
    void createLayout();
    void createPages();
    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void createButtons();
    void readCheckBoxState();
    void setLogCheckBox();
    void handleOkButton();
};

#endif // SETTINGDIALOG_H
