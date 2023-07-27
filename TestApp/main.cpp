#include <QtWidgets/QApplication>

#include "TestApp.h"

// class Q_CORE_EXPORT QObject {
//    // Connect a signal to a pointer to qobject member function
//    template <typename Func1, typename Func2>
//    static inline QMetaObject::Connection connect(
//        const typename QtPrivate::FunctionPointer<Func1>::Object *sender,
//        Func1 signal,
//        const typename QtPrivate::FunctionPointer<Func2>::Object *receiver,
//        Func2 slot, Qt::ConnectionType type = Qt::AutoConnection) {
//        //...
//    }
//
//    //...
//    // Connect a signal to a pointer to qobject member function
//    template <typename Func1, typename Func2>
//    static inline QMetaObject::Connection connect(
//        const typename QtPrivate::FunctionPointer<Func1>::Object *sender,
//        Func1 signal,
//        const typename QtPrivate::FunctionPointer<Func2>::Object *receiver,
//        Func2 slot, Qt::ConnectionType type = Qt::AutoConnection) {
//        //...
//        const int *types = nullptr;
//        if (type == Qt::QueuedConnection ||
//            type == Qt::BlockingQueuedConnection)
//            types = QtPrivate::ConnectionTypes<
//                typename SignalType::Arguments>::types();
//
//        return connectImpl(
//            sender, reinterpret_cast<void **>(&signal), receiver,
//            reinterpret_cast<void **>(&slot),
//            new QtPrivate::QSlotObject<
//                Func2,
//                typename QtPrivate::List_Left<typename SignalType::Arguments,
//                                              SlotType::ArgumentCount>::Value,
//                typename SignalType::ReturnType>(slot),
//            type, types, &SignalType::Object::staticMetaObject);
//    }
//};

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QApplication a(argc, argv);
    TestApp w;
    w.init();
    w.show();
    return a.exec();
}

template <bool callbacks_enabled>
void doActivate(QObject *sender, int signal_index, void **argv) {
    QObjectPrivate *sp = QObjectPrivate::get(sender);

    if (sp->blockSig) return;

    Q_TRACE_SCOPE(QMetaObject_activate, sender, signal_index);

    if (sp->isDeclarativeSignalConnected(signal_index) &&
        QAbstractDeclarativeData::signalEmitted) {
        Q_TRACE_SCOPE(QMetaObject_activate_declarative_signal, sender,
                      signal_index);
        QAbstractDeclarativeData::signalEmitted(sp->declarativeData, sender,
                                                signal_index, argv);
    }

    const QSignalSpyCallbackSet *signal_spy_set =
        callbacks_enabled ? qt_signal_spy_callback_set.loadAcquire() : nullptr;

    void *empty_argv[] = {nullptr};
    if (!argv) argv = empty_argv;

    if (!sp->maybeSignalConnected(signal_index)) {
        // The possible declarative connection is done, and nothing else is
        // connected
        if (callbacks_enabled &&
            signal_spy_set->signal_begin_callback != nullptr)
            signal_spy_set->signal_begin_callback(sender, signal_index, argv);
        if (callbacks_enabled && signal_spy_set->signal_end_callback != nullptr)
            signal_spy_set->signal_end_callback(sender, signal_index);
        return;
    }

    if (callbacks_enabled && signal_spy_set->signal_begin_callback != nullptr)
        signal_spy_set->signal_begin_callback(sender, signal_index, argv);

    bool senderDeleted = false;
    {
        Q_ASSERT(sp->connections.loadAcquire());
        QObjectPrivate::ConnectionDataPointer connections(
            sp->connections.loadRelaxed());
        QObjectPrivate::SignalVector *signalVector =
            connections->signalVector.loadRelaxed();

        const QObjectPrivate::ConnectionList *list;
        if (signal_index < signalVector->count())
            list = &signalVector->at(signal_index);
        else
            list = &signalVector->at(-1);

        Qt::HANDLE currentThreadId = QThread::currentThreadId();
        bool inSenderThread =
            currentThreadId ==
            QObjectPrivate::get(sender)->threadData->threadId.loadRelaxed();

        // We need to check against the highest connection id to ensure that
        // signals added during the signal emission are not emitted in this
        // emission.
        uint highestConnectionId =
            connections->currentConnectionId.loadRelaxed();
        do {
            QObjectPrivate::Connection *c = list->first.loadRelaxed();
            if (!c) continue;

            do {
                QObject *const receiver = c->receiver.loadRelaxed();
                if (!receiver) continue;

                QThreadData *td = c->receiverThreadData.loadRelaxed();
                if (!td) continue;

                bool receiverInSameThread;
                if (inSenderThread) {
                    receiverInSameThread =
                        currentThreadId == td->threadId.loadRelaxed();
                } else {
                    // need to lock before reading the threadId, because
                    // moveToThread() could interfere
                    QMutexLocker lock(signalSlotLock(receiver));
                    receiverInSameThread =
                        currentThreadId == td->threadId.loadRelaxed();
                }

                // determine if this connection should be sent immediately or
                // put into the event queue
                if ((c->connectionType == Qt::AutoConnection &&
                     !receiverInSameThread) ||
                    (c->connectionType == Qt::QueuedConnection)) {
                    queued_activate(sender, signal_index, c, argv);
                    continue;
#if QT_CONFIG(thread)
                } else if (c->connectionType == Qt::BlockingQueuedConnection) {
                    if (receiverInSameThread) {
                        qWarning(
                            "Qt: Dead lock detected while activating a "
                            "BlockingQueuedConnection: "
                            "Sender is %s(%p), receiver is %s(%p)",
                            sender->metaObject()->className(), sender,
                            receiver->metaObject()->className(), receiver);
                    }
                    QSemaphore semaphore;
                    {
                        QBasicMutexLocker locker(signalSlotLock(sender));
                        if (!c->receiver.loadAcquire()) continue;
                        QMetaCallEvent *ev =
                            c->isSlotObject
                                ? new QMetaCallEvent(c->slotObj, sender,
                                                     signal_index, argv,
                                                     &semaphore)
                                : new QMetaCallEvent(
                                      c->method_offset, c->method_relative,
                                      c->callFunction, sender, signal_index,
                                      argv, &semaphore);
                        QCoreApplication::postEvent(receiver, ev);
                    }
                    semaphore.acquire();
                    continue;
#endif
                }

                QObjectPrivate::Sender senderData(
                    receiverInSameThread ? receiver : nullptr, sender,
                    signal_index);

                if (c->isSlotObject) {
                    c->slotObj->ref();

                    struct Deleter {
                        void operator()(
                            QtPrivate::QSlotObjectBase *slot) const {
                            if (slot) slot->destroyIfLastRef();
                        }
                    };
                    const std::unique_ptr<QtPrivate::QSlotObjectBase, Deleter>
                        obj{c->slotObj};

                    {
                        Q_TRACE_SCOPE(QMetaObject_activate_slot_functor,
                                      obj.get());
                        obj->call(receiver, argv);
                    }
                } else if (c->callFunction &&
                           c->method_offset <=
                               receiver->metaObject()->methodOffset()) {
                    // we compare the vtable to make sure we are not in the
                    // destructor of the object.
                    const int method_relative = c->method_relative;
                    const auto callFunction = c->callFunction;
                    const int methodIndex =
                        (Q_HAS_TRACEPOINTS || callbacks_enabled) ? c->method()
                                                                 : 0;
                    if (callbacks_enabled &&
                        signal_spy_set->slot_begin_callback != nullptr)
                        signal_spy_set->slot_begin_callback(receiver,
                                                            methodIndex, argv);

                    {
                        Q_TRACE_SCOPE(QMetaObject_activate_slot, receiver,
                                      methodIndex);
                        callFunction(receiver, QMetaObject::InvokeMetaMethod,
                                     method_relative, argv);
                    }

                    if (callbacks_enabled &&
                        signal_spy_set->slot_end_callback != nullptr)
                        signal_spy_set->slot_end_callback(receiver,
                                                          methodIndex);
                } else {
                    const int method = c->method_relative + c->method_offset;

                    if (callbacks_enabled &&
                        signal_spy_set->slot_begin_callback != nullptr) {
                        signal_spy_set->slot_begin_callback(receiver, method,
                                                            argv);
                    }

                    {
                        Q_TRACE_SCOPE(QMetaObject_activate_slot, receiver,
                                      method);
                        QMetaObject::metacall(receiver,
                                              QMetaObject::InvokeMetaMethod,
                                              method, argv);
                    }

                    if (callbacks_enabled &&
                        signal_spy_set->slot_end_callback != nullptr)
                        signal_spy_set->slot_end_callback(receiver, method);
                }
            } while ((c = c->nextConnectionList.loadRelaxed()) != nullptr &&
                     c->id <= highestConnectionId);

        } while (list != &signalVector->at(-1) &&
                 // start over for all signals;
                 ((list = &signalVector->at(-1)), true));

        if (connections->currentConnectionId.loadRelaxed() == 0)
            senderDeleted = true;
    }
    // ...
}