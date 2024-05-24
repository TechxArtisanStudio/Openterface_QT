// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "ui/mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QLoggingCategory>

void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    const char *file = context.file ? context.file : "";
    const char *function = context.function ? context.function : "";
    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        abort();
    }
}


int main(int argc, char *argv[])
{
    qDebug() << "Start openterface...";
    QApplication app(argc, argv);

    //qInstallMessageHandler(customMessageHandler);
    QLoggingCategory::setFilterRules("opf.core.*=true\n"
                                     "opf.ui.*=true\n"
                                     "opf.host.*=true\n"
                                     "opf.core.serial=false\n");

    QCoreApplication::setApplicationName("Openterface Mini-KVM");
    QCoreApplication::setOrganizationName("TechxArtisan");
    QCoreApplication::setApplicationVersion("0.0.1");

    app.setWindowIcon(QIcon("://images/icon_32.png"));

    Camera camera;
    camera.show();

    return app.exec();
};
