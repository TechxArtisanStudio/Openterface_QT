#ifndef TASKMANAGER_H
#define TASKMANAGER_H

#pragma once

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <functional>

class TaskManager : public QObject
{
    Q_OBJECT
public:
    static TaskManager* instance();
    void addTask(std::function<void()> task);

private:
    TaskManager();
    ~TaskManager();

    class Worker;
    Worker *m_worker;
    QThread m_workerThread;
};

class TaskManager::Worker : public QObject
{
    Q_OBJECT
    friend class TaskManager;

public:
    Worker();
    ~Worker();

private slots:
    void onProcessTasks();

private:
    QQueue<std::function<void()>> m_taskQueue;
    QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_exit = false;
};

#endif // TASKMANAGER_H