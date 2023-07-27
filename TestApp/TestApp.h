#pragma once

#include <QtWidgets/QMainWindow>

#include "WorkThread.h"
#include "ui_TestApp.h"

class TestThread : public WorkThread {
    Q_OBJECT
 public:
    TestThread(QObject* parent = Q_NULLPTR) : WorkThread(parent) {}

 signals:
    void sigThreadNotify(qint64 task, const QString& data);

 private:
    virtual void handleTask(qint64 task) override {
        emit sigThreadNotify(task, "hello world!");
    }
};

//////////////////////////////////////////////////////////////////////////

class TestApp : public QMainWindow {
    Q_OBJECT

 public:
    TestApp(QWidget* parent = Q_NULLPTR);
    ~TestApp() {}

    void init();
    void appendThreadTask(qint64 task);

 public slots:
    void slotThreadNotify(qint64 task, const QString& data);

 private:
    void initThread();

 private:
    Ui::TestAppClass ui;
    TestThread* m_thread = Q_NULLPTR;
};
