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

#ifndef IMAGESETTINGS_H
#define IMAGESETTINGS_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QComboBox;
class QImageCapture;
namespace Ui {
class ImageSettingsUi;
}
QT_END_NAMESPACE

class ImageSettings : public QDialog
{
    Q_OBJECT

public:
    explicit ImageSettings(QImageCapture *imageCapture, QWidget *parent = nullptr);
    ~ImageSettings();

    void applyImageSettings() const;

    QString format() const;
    void setFormat(const QString &format);

protected:
    void changeEvent(QEvent *e) override;

private:
    QVariant boxValue(const QComboBox *box) const;
    void selectComboBoxItem(QComboBox *box, const QVariant &value);

    Ui::ImageSettingsUi *ui;
    QImageCapture *imagecapture;
};

#endif // IMAGESETTINGS_H
