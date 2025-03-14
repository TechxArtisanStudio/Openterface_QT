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

#include "metadatadialog.h"

#include <QMediaMetaData>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

#include <QString>

static QString defaultValue(QMediaMetaData::Key key)
{
    switch (key) {
    case QMediaMetaData::Title:
        return MetaDataDialog::tr("Openterface Mini KVM");
    case QMediaMetaData::Author:
        return MetaDataDialog::tr("TechxArtisan");
    case QMediaMetaData::Date:
        return QDateTime::currentDateTime().toString();
    default:
        break;
    }
    return {};
}

MetaDataDialog::MetaDataDialog(QWidget *parent) : QDialog(parent)
{
    auto *viewport = new QWidget;
    auto *metaDataLayout = new QFormLayout(viewport);

    for (int i = 0; i < QMediaMetaData::NumMetaData; ++i) {
        const auto key = static_cast<QMediaMetaData::Key>(i);
        QString label = QMediaMetaData::metaDataKeyToString(key);
        auto *lineEdit = new QLineEdit(defaultValue(key));
        lineEdit->setClearButtonEnabled(true);
        m_metaDataFields[key] = lineEdit;

        switch (key) {
        case QMediaMetaData::ThumbnailImage: {
            QPushButton *openThumbnail = new QPushButton(tr("Open"));
            connect(openThumbnail, &QPushButton::clicked, this,
                    &MetaDataDialog::openThumbnailImage);
            QHBoxLayout *layout = new QHBoxLayout;
            layout->addWidget(lineEdit);
            layout->addWidget(openThumbnail);
            metaDataLayout->addRow(label, layout);
        }
        break;
        case QMediaMetaData::CoverArtImage: {
            QPushButton *openCoverArt = new QPushButton(tr("Open"));
            connect(openCoverArt, &QPushButton::clicked, this, &MetaDataDialog::openCoverArtImage);
            QHBoxLayout *layout = new QHBoxLayout;
            layout->addWidget(lineEdit);
            layout->addWidget(openCoverArt);
            metaDataLayout->addRow(label, layout);
        }
        break;
        default:
            metaDataLayout->addRow(label, lineEdit);
            break;
        }
    }

    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidget(viewport);
    auto *dialogLayout = new QVBoxLayout(this);
    dialogLayout->addWidget(scrollArea);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    dialogLayout->addWidget(buttonBox);

    setWindowTitle(tr("Set Metadata"));
    resize(400, 300);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &MetaDataDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &MetaDataDialog::reject);
}

void MetaDataDialog::openThumbnailImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Image"), QDir::currentPath(),
                                                    tr("Image Files (*.png *.jpg *.bmp)"));
    if (!fileName.isEmpty())
        m_metaDataFields[QMediaMetaData::ThumbnailImage]->setText(fileName);
}

void MetaDataDialog::openCoverArtImage()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("Open Image"), QDir::currentPath(),
                                                    tr("Image Files (*.png *.jpg *.bmp)"));
    if (!fileName.isEmpty())
        m_metaDataFields[QMediaMetaData::CoverArtImage]->setText(fileName);
}
