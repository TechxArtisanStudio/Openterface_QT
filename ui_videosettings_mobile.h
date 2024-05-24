/********************************************************************************
** Form generated from reading UI file 'videosettings_mobile.ui'
**
** Created by: Qt User Interface Compiler version 6.5.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_VIDEOSETTINGS_MOBILE_H
#define UI_VIDEOSETTINGS_MOBILE_H

#include <QtCore/QVariant>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_VideoSettingsUi
{
public:
    QGridLayout *gridLayout_3;
    QWidget *widget;
    QVBoxLayout *verticalLayout_3;
    QGroupBox *groupBox_3;
    QVBoxLayout *verticalLayout_2;
    QLabel *label_2;
    QComboBox *audioCodecBox;
    QLabel *label_5;
    QSpinBox *audioSampleRateBox;
    QGroupBox *groupBox;
    QVBoxLayout *verticalLayout;
    QLabel *label_3;
    QSlider *qualitySlider;
    QLabel *label_4;
    QComboBox *containerFormatBox;
    QGroupBox *groupBox_2;
    QGridLayout *gridLayout_2;
    QLabel *label;
    QComboBox *videoCodecBox;
    QLabel *label_8;
    QLabel *label_6;
    QComboBox *videoFormatBox;
    QDialogButtonBox *buttonBox;
    QHBoxLayout *horizontalLayout;
    QSpinBox *fpsSpinBox;
    QSlider *fpsSlider;

    void setupUi(QDialog *VideoSettingsUi)
    {
        if (VideoSettingsUi->objectName().isEmpty())
            VideoSettingsUi->setObjectName("VideoSettingsUi");
        VideoSettingsUi->resize(329, 591);
        gridLayout_3 = new QGridLayout(VideoSettingsUi);
        gridLayout_3->setObjectName("gridLayout_3");
        widget = new QWidget(VideoSettingsUi);
        widget->setObjectName("widget");
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(widget->sizePolicy().hasHeightForWidth());
        widget->setSizePolicy(sizePolicy);
        verticalLayout_3 = new QVBoxLayout(widget);
        verticalLayout_3->setObjectName("verticalLayout_3");
        verticalLayout_3->setContentsMargins(0, 0, 0, 0);
        groupBox_3 = new QGroupBox(widget);
        groupBox_3->setObjectName("groupBox_3");
        verticalLayout_2 = new QVBoxLayout(groupBox_3);
        verticalLayout_2->setObjectName("verticalLayout_2");
        label_2 = new QLabel(groupBox_3);
        label_2->setObjectName("label_2");

        verticalLayout_2->addWidget(label_2);

        audioCodecBox = new QComboBox(groupBox_3);
        audioCodecBox->setObjectName("audioCodecBox");

        verticalLayout_2->addWidget(audioCodecBox);

        label_5 = new QLabel(groupBox_3);
        label_5->setObjectName("label_5");

        verticalLayout_2->addWidget(label_5);

        audioSampleRateBox = new QSpinBox(groupBox_3);
        audioSampleRateBox->setObjectName("audioSampleRateBox");

        verticalLayout_2->addWidget(audioSampleRateBox);


        verticalLayout_3->addWidget(groupBox_3);

        groupBox = new QGroupBox(widget);
        groupBox->setObjectName("groupBox");
        verticalLayout = new QVBoxLayout(groupBox);
        verticalLayout->setObjectName("verticalLayout");
        label_3 = new QLabel(groupBox);
        label_3->setObjectName("label_3");

        verticalLayout->addWidget(label_3);

        qualitySlider = new QSlider(groupBox);
        qualitySlider->setObjectName("qualitySlider");
        qualitySlider->setMaximum(4);
        qualitySlider->setOrientation(Qt::Horizontal);

        verticalLayout->addWidget(qualitySlider);

        label_4 = new QLabel(groupBox);
        label_4->setObjectName("label_4");

        verticalLayout->addWidget(label_4);

        containerFormatBox = new QComboBox(groupBox);
        containerFormatBox->setObjectName("containerFormatBox");

        verticalLayout->addWidget(containerFormatBox);


        verticalLayout_3->addWidget(groupBox);


        gridLayout_3->addWidget(widget, 2, 0, 1, 1);

        groupBox_2 = new QGroupBox(VideoSettingsUi);
        groupBox_2->setObjectName("groupBox_2");
        gridLayout_2 = new QGridLayout(groupBox_2);
        gridLayout_2->setObjectName("gridLayout_2");
        label = new QLabel(groupBox_2);
        label->setObjectName("label");

        gridLayout_2->addWidget(label, 2, 0, 1, 1);

        videoCodecBox = new QComboBox(groupBox_2);
        videoCodecBox->setObjectName("videoCodecBox");

        gridLayout_2->addWidget(videoCodecBox, 6, 0, 1, 2);

        label_8 = new QLabel(groupBox_2);
        label_8->setObjectName("label_8");

        gridLayout_2->addWidget(label_8, 0, 0, 1, 2);

        label_6 = new QLabel(groupBox_2);
        label_6->setObjectName("label_6");

        gridLayout_2->addWidget(label_6, 5, 0, 1, 2);

        videoFormatBox = new QComboBox(groupBox_2);
        videoFormatBox->setObjectName("videoFormatBox");

        gridLayout_2->addWidget(videoFormatBox, 1, 0, 1, 2);

        buttonBox = new QDialogButtonBox(groupBox_2);
        buttonBox->setObjectName("buttonBox");
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        gridLayout_2->addWidget(buttonBox, 7, 0, 1, 1);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName("horizontalLayout");
        fpsSpinBox = new QSpinBox(groupBox_2);
        fpsSpinBox->setObjectName("fpsSpinBox");
        fpsSpinBox->setMinimum(8);
        fpsSpinBox->setMaximum(30);

        horizontalLayout->addWidget(fpsSpinBox);

        fpsSlider = new QSlider(groupBox_2);
        fpsSlider->setObjectName("fpsSlider");
        fpsSlider->setOrientation(Qt::Horizontal);

        horizontalLayout->addWidget(fpsSlider);


        gridLayout_2->addLayout(horizontalLayout, 3, 0, 1, 1);


        gridLayout_3->addWidget(groupBox_2, 3, 0, 1, 1);


        retranslateUi(VideoSettingsUi);
        QObject::connect(buttonBox, &QDialogButtonBox::accepted, VideoSettingsUi, qOverload<>(&QDialog::accept));
        QObject::connect(buttonBox, &QDialogButtonBox::rejected, VideoSettingsUi, qOverload<>(&QDialog::reject));

        QMetaObject::connectSlotsByName(VideoSettingsUi);
    } // setupUi

    void retranslateUi(QDialog *VideoSettingsUi)
    {
        VideoSettingsUi->setWindowTitle(QCoreApplication::translate("VideoSettingsUi", "Video Settings", nullptr));
        groupBox_3->setTitle(QCoreApplication::translate("VideoSettingsUi", "Audio", nullptr));
        label_2->setText(QCoreApplication::translate("VideoSettingsUi", "Audio Codec:", nullptr));
        label_5->setText(QCoreApplication::translate("VideoSettingsUi", "Sample Rate:", nullptr));
        label_3->setText(QCoreApplication::translate("VideoSettingsUi", "Quality:", nullptr));
        label_4->setText(QCoreApplication::translate("VideoSettingsUi", "File Format:", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("VideoSettingsUi", "Video", nullptr));
        label->setText(QCoreApplication::translate("VideoSettingsUi", "Frames per second:", nullptr));
        label_8->setText(QCoreApplication::translate("VideoSettingsUi", "Camera Format:", nullptr));
        label_6->setText(QCoreApplication::translate("VideoSettingsUi", "Video Codec:", nullptr));
    } // retranslateUi

};

namespace Ui {
    class VideoSettingsUi: public Ui_VideoSettingsUi {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_VIDEOSETTINGS_MOBILE_H
