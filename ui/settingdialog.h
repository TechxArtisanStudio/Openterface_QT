#ifndef SETTINGDIALOG_H
#define SETTINGDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QStackedWidget>

namespace Ui {
class settingDialog;
}

class settingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit settingDialog(QWidget *parent = nullptr);
    ~settingDialog();

private:
    Ui::settingDialog *ui;
    QTreeWidget *settingTree;
    QStackedWidget *stackedWidget;


    void switchWidgetShow(QString &btnName);
    void createSettingTree();
    void createLayout();
    void createPages();
    void changePage(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    
};

#endif // SETTINGDIALOG_H
