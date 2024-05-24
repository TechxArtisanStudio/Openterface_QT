/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.5.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QActionGroup>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_Camera
{
public:
    QAction *actionExit;
    QAction *actionStartCamera;
    QAction *actionStopCamera;
    QAction *actionSettings;
    QAction *actionResetHID;
    QActionGroup *actionGroup;
    QAction *actionAbsolute;
    QAction *actionRelative;
    QWidget *centralwidget;
    QGridLayout *gridLayout_3;
    QMenuBar *menubar;
    QMenu *menuFile;
    QMenu *menuSource;
    QMenu *menuControl;
    QMenu *menuMouse_Mode;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *Camera)
    {
        if (Camera->objectName().isEmpty())
            Camera->setObjectName("Camera");
        Camera->resize(668, 429);
        Camera->setCursor(QCursor(Qt::ArrowCursor));
        Camera->setMouseTracking(true);
        actionExit = new QAction(Camera);
        actionExit->setObjectName("actionExit");
        actionStartCamera = new QAction(Camera);
        actionStartCamera->setObjectName("actionStartCamera");
        actionStopCamera = new QAction(Camera);
        actionStopCamera->setObjectName("actionStopCamera");
        actionSettings = new QAction(Camera);
        actionSettings->setObjectName("actionSettings");
        actionResetHID = new QAction(Camera);
        actionResetHID->setObjectName("actionResetHID");
        actionGroup = new QActionGroup(Camera);
        actionGroup->setObjectName("actionGroup");
        actionAbsolute = new QAction(actionGroup);
        actionAbsolute->setObjectName("actionAbsolute");
        actionAbsolute->setCheckable(true);
        actionAbsolute->setChecked(true);
        actionRelative = new QAction(actionGroup);
        actionRelative->setObjectName("actionRelative");
        actionRelative->setCheckable(true);
        centralwidget = new QWidget(Camera);
        centralwidget->setObjectName("centralwidget");
        centralwidget->setCursor(QCursor(Qt::ArrowCursor));
        centralwidget->setMouseTracking(true);
        gridLayout_3 = new QGridLayout(centralwidget);
        gridLayout_3->setSpacing(0);
        gridLayout_3->setObjectName("gridLayout_3");
        gridLayout_3->setContentsMargins(0, 0, 0, 0);
        Camera->setCentralWidget(centralwidget);
        menubar = new QMenuBar(Camera);
        menubar->setObjectName("menubar");
        menubar->setGeometry(QRect(0, 0, 668, 21));
        menuFile = new QMenu(menubar);
        menuFile->setObjectName("menuFile");
        menuSource = new QMenu(menubar);
        menuSource->setObjectName("menuSource");
        menuControl = new QMenu(menubar);
        menuControl->setObjectName("menuControl");
        menuMouse_Mode = new QMenu(menuControl);
        menuMouse_Mode->setObjectName("menuMouse_Mode");
        Camera->setMenuBar(menubar);
        statusbar = new QStatusBar(Camera);
        statusbar->setObjectName("statusbar");
        Camera->setStatusBar(statusbar);

        menubar->addAction(menuFile->menuAction());
        menubar->addAction(menuControl->menuAction());
        menubar->addAction(menuSource->menuAction());
        menuFile->addSeparator();
        menuFile->addAction(actionSettings);
        menuFile->addSeparator();
        menuFile->addAction(actionExit);
        menuControl->addAction(menuMouse_Mode->menuAction());
        menuControl->addAction(actionResetHID);
        menuMouse_Mode->addAction(actionAbsolute);
        menuMouse_Mode->addAction(actionRelative);

        retranslateUi(Camera);
        QObject::connect(actionExit, &QAction::triggered, Camera, qOverload<>(&QMainWindow::close));
        QObject::connect(actionSettings, SIGNAL(triggered()), Camera, SLOT(configureCaptureSettings()));
        QObject::connect(actionStartCamera, SIGNAL(triggered()), Camera, SLOT(startCamera()));
        QObject::connect(actionStopCamera, SIGNAL(triggered()), Camera, SLOT(stopCamera()));

        QMetaObject::connectSlotsByName(Camera);
    } // setupUi

    void retranslateUi(QMainWindow *Camera)
    {
        Camera->setWindowTitle(QCoreApplication::translate("Camera", "Camera", nullptr));
        actionExit->setText(QCoreApplication::translate("Camera", "Close", nullptr));
        actionStartCamera->setText(QCoreApplication::translate("Camera", "Start Camera", nullptr));
        actionStopCamera->setText(QCoreApplication::translate("Camera", "Stop Camera", nullptr));
        actionSettings->setText(QCoreApplication::translate("Camera", "Change Settings", nullptr));
        actionResetHID->setText(QCoreApplication::translate("Camera", "ResetHID", nullptr));
        actionAbsolute->setText(QCoreApplication::translate("Camera", "Absolute", nullptr));
        actionRelative->setText(QCoreApplication::translate("Camera", "Relative", nullptr));
        menuFile->setTitle(QCoreApplication::translate("Camera", "File", nullptr));
        menuSource->setTitle(QCoreApplication::translate("Camera", "Source", nullptr));
        menuControl->setTitle(QCoreApplication::translate("Camera", "Control", nullptr));
        menuMouse_Mode->setTitle(QCoreApplication::translate("Camera", "Mouse Mode", nullptr));
    } // retranslateUi

};

namespace Ui {
    class Camera: public Ui_Camera {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
