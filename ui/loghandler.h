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


#ifndef LOGHANDLER_H
#define LOGHANDLER_H

#include <QObject>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <iostream>
#include <QApplication>
#include <QIcon>
#include <QDateTime>
#include <QDebug>
#include <QThread>
#include <QLoggingCategory>
#include <QStyleFactory>

class LogHandler : public QObject
{
    Q_OBJECT

public:
    explicit LogHandler(QObject *parent = nullptr); 

    static LogHandler& instance();

    void enableLogStore();
    
    static void fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
    static void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg);
};


#endif // LOGHANDLER_H
