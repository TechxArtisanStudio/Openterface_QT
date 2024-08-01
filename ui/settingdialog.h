#ifndef SETTINGDIALOG_H
#define SETTINGDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
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
    
    void switchWidgetShow(QString &btnName);
    void createSettingTree();
    void createLayout();

};

#endif // SETTINGDIALOG_H
