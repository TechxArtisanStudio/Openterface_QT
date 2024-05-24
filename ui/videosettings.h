// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

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
