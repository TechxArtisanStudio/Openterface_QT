/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation version 3.                                 *
*                                                                            *
*    This program is distributed in the hope that it will be useful, but     *
*    WITHOUT ANY WARRANTY; without even the implied warranty of              *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU        *
*    General Public License for more details.                                *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see <http://www.gnu.org/licenses/>.    *
*                                                                            *
* ========================================================================== *
*/


#ifndef SERIALPORTDEBUGDIALOG_H
#define SERIALPORTDEBUGDIALOG_H

#include <QDialog>
#include <QTextEdit>

class SerialPortDebugDialog : public QDialog {
    Q_OBJECT
public:
    explicit SerialPortDebugDialog(QWidget *parent = nullptr);

private slots:
    void handleSerialData(const QByteArray &data, bool isReceived);
    void getRecvDataAndInsertText(const QByteArray &data) { handleSerialData(data, true); }
    void getSentDataAndInsertText(const QByteArray &data) { handleSerialData(data, false); }

private:
    QTextEdit *textEdit;
    QWidget *debugButtonWidget;
    QWidget *filterCheckboxWidget;

    struct FilterSettings {
        const char* name;
        const char* label;
        unsigned char recvCode;
        unsigned char sendCode;
    };
    static const FilterSettings FILTERS[];

    void createDebugButtonWidget();
    void createFilterCheckBox();
    void createLayout();
    void saveSettings();
    void loadSettings();
    QString formatHexData(const QString &hexString);
    bool shouldShowMessage(unsigned char code) const;
    QString getCommandType(unsigned char code) const;
};

#endif // SERIALPORTDEBUGDIALOG_H
