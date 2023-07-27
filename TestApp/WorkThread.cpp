#include "WorkThread.h"

void WorkThread::stopThread() {
    if (!m_shutdown) {
        m_shutdown = true;
        m_condition.notify_all();
        QThread::usleep(100);
    }
}

void WorkThread::appendTask(qint64 task) {
    if (!m_shutdown) {
        QMutexLocker locker(&m_mtx);
        m_tasks.append(task);
        m_condition.notify_one();
    }
}

qint64 WorkThread::popTask() {
    QMutexLocker locker(&m_mtx);

    qint64 task = -1;
    if (!m_tasks.isEmpty()) {
        task = m_tasks.front();
        m_tasks.pop_front();
    }
    return task;
}

void WorkThread::run() {
    while (!m_shutdown) {
        {
            QMutexLocker locker(&m_mtx);
            if (m_tasks.isEmpty()) {
                m_condition.wait(&m_mtx);
            }
        }

        auto task = popTask();
        if (task != INVALID_TASK) {
            handleTask(task);
        }
    }
}
