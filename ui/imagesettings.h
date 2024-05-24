// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

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
