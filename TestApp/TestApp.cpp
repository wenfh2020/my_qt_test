#include "TestApp.h"

#include <QtDebug>
#include <QtWidgets/QMessageBox>

#define TASK_ID 1

TestApp::TestApp(QWidget* parent) : QMainWindow(parent) { ui.setupUi(this); }

void TestApp::init() {
    initThread();
    connect(m_thread, &TestThread::sigThreadNotify, this,
            &TestApp::slotThreadNotify);
    connect(ui.btn_work_thread, &QPushButton::clicked, this, [this]() {
        auto msg = QString("send task to work thread, task: %1").arg(TASK_ID);
        QMessageBox::information(this, "tip", msg, QMessageBox::Yes);
        this->appendThreadTask(TASK_ID);
    });
}

void TestApp::slotThreadNotify(qint64 task, const QString& data) {
    auto msg = QString("response from work thread, task: %1, data: %2")
                   .arg(task)
                   .arg(data);
    QMessageBox::information(this, "tip", msg, QMessageBox::Yes);
}

void TestApp::initThread() {
    if (!m_thread) {
        m_thread = new TestThread(this);
        m_thread->start();
    }
}

void TestApp::appendThreadTask(qint64 task) {
    if (m_thread) {
        m_thread->appendTask(task);
    }
}
