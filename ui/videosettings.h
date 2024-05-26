/*
* ========================================================================== *
*                                                                            *
*    This file is part of the Openterface Mini KVM App QT version            *
*                                                                            *
*    Copyright (C) 2024   <info@openterface.com>                             *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
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

#ifndef VIDEOSETTINGS_H
#define VIDEOSETTINGS_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QCameraFormat;
class QComboBox;
class QMediaRecorder;
namespace Ui {
class VideoSettingsUi;
}
QT_END_NAMESPACE

class VideoSettings : public QDialog
{
    Q_OBJECT

public:
    explicit VideoSettings(QMediaRecorder *mediaRecorder, QWidget *parent = nullptr);
    ~VideoSettings();

    void applySettings();
    void updateFormatsAndCodecs();

protected:
    void changeEvent(QEvent *e) override;

private:
    void setFpsRange(const QCameraFormat &format);
    QVariant boxValue(const QComboBox *) const;
    void selectComboBoxItem(QComboBox *box, const QVariant &value);

    Ui::VideoSettingsUi *ui;
    QMediaRecorder *mediaRecorder;
    bool m_updatingFormats = false;
};

#endif // VIDEOSETTINGS_H
