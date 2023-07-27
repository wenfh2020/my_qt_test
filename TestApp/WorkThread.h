#pragma once

#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#define INVALID_TASK -1

class WorkThread : public QThread {
 public:
    WorkThread(QObject* parent = nullptr) : QThread(parent) {}
    ~WorkThread() { stopThread(); }

    void stopThread();

    void appendTask(qint64 task);
    qint64 WorkThread::popTask();
    virtual void handleTask(qint64 task) {}

 private:
    // 线程运行函数
    void run();

 private:
    QList<qint64> m_tasks;

    bool m_shutdown = false;
    QMutex m_mtx;
    QWaitCondition m_condition;
};