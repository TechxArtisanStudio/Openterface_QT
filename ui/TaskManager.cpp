#include "TaskManager.h"

TaskManager* TaskManager::instance()
{
    static TaskManager instance;
    return &instance;
}

TaskManager::TaskManager() : m_worker(new Worker()), m_workerThread()
{
    m_worker->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::started, m_worker, &Worker::onProcessTasks);
    m_workerThread.start();
}

TaskManager::~TaskManager()
{
    {
        QMutexLocker locker(&m_worker->m_mutex);
        m_worker->m_exit = true;
    }
    m_worker->m_condition.wakeOne();
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
}

void TaskManager::addTask(std::function<void()> task)
{
    {
        QMutexLocker locker(&m_worker->m_mutex);
        m_worker->m_taskQueue.enqueue(task);
    }
    m_worker->m_condition.wakeOne();
}

TaskManager::Worker::Worker() : m_exit(false)
{
}

TaskManager::Worker::~Worker()
{
}

void TaskManager::Worker::onProcessTasks()
{
    while (!m_exit) {
        std::function<void()> task;
        {
            QMutexLocker locker(&m_mutex);
            if (m_taskQueue.isEmpty()) {
                m_condition.wait(&m_mutex);
            }
            if (!m_taskQueue.isEmpty()) {
                task = m_taskQueue.dequeue();
            }
        }
        if (task) {
            task();
        }
    }
}