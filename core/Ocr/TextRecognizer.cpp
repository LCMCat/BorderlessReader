#include "core/Ocr/TextRecognizer.h"

#include "core/Ocr/OcrBackend.h"

#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QWaitCondition>

#include <deque>
#include <memory>
#include <utility>

namespace Ocr
{

// ---------------------------------------------------------------------------
// 工作线程：初始化一次后端，然后串行消费请求队列。
//
// 用一条常驻线程而不是“每次识别新建 std::thread”，原因有三：
//  1. WinRT / Tesseract 的引擎句柄本来就是稀缺资源，重复初始化代价高；
//  2. 串行化后不会出现多个识别同时抢引擎、把内存打满；
//  3. 退出路径只有一处：置退出标志 → 唤醒 → join，不会有游离线程。
//
// 请求不做超时：单次 OCR 耗时由后端决定，Recognizer 不代替业务判断“多久算失败”。
// ---------------------------------------------------------------------------
class Recognizer::Worker
{
public:
    explicit Worker(Recognizer *owner)
        : m_owner(owner)
    {
    }

    ~Worker()
    {
        stop();
    }

    void start()
    {
        if (m_thread)
            return;
        m_thread = QThread::create([this]() { run(); });
        m_thread->setObjectName(QStringLiteral("OcrWorker"));
        m_thread->start();
    }

    void stop()
    {
        if (!m_thread)
            return;

        {
            QMutexLocker locker(&m_mutex);
            m_quitting = true;
            m_wake.wakeAll();
        }

        // 正在进行的那次识别无法中途打断（后端是同步 C API），等它结束。
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }

    void submit(RequestId id, const QImage &image, Callback callback)
    {
        QMutexLocker locker(&m_mutex);
        m_queue.push_back(Job{ id, image, std::move(callback) });
        m_wake.wakeOne();
    }

    void cancel(RequestId id)
    {
        QMutexLocker locker(&m_mutex);
        if (!m_cancelled.contains(id))
            m_cancelled.append(id);
    }

private:
    struct Job
    {
        RequestId id = 0;
        QImage image;
        Callback callback;
    };

    bool takeJob(Job *job)
    {
        QMutexLocker locker(&m_mutex);
        for (;;) {
            if (m_quitting)
                return false;

            if (!m_queue.empty()) {
                Job next = std::move(m_queue.front());
                m_queue.pop_front();
                if (m_cancelled.contains(next.id))
                    continue;   // 排队期间被取消，不必再跑
                *job = std::move(next);
                return true;
            }

            m_wake.wait(&m_mutex);
        }
    }

    void run()
    {
        std::unique_ptr<Backend> backend = createPlatformBackend();
        QString initError;
        bool ready = false;

        if (!backend) {
            initError = QStringLiteral("当前构建没有启用 OCR 后端。");
        } else {
            ready = backend->initialize(&initError);
        }

        postState(ready, ready ? backend->description() : QString(), initError);

        Job job;
        while (takeJob(&job)) {
            Result result;
            if (ready) {
                result = backend->recognize(job.image);
            } else {
                result.error = initError;
            }
            postResult(job.id, job.callback, result);
        }
    }

    void postState(bool ready, const QString &description, const QString &reason)
    {
        Recognizer *owner = m_owner;
        QMetaObject::invokeMethod(
            owner,
            [owner, ready, description, reason]() {
                owner->applyState(ready, description, reason);
            },
            Qt::QueuedConnection);
    }

    void postResult(RequestId id, const Callback &callback, const Result &result)
    {
        Recognizer *owner = m_owner;
        QMetaObject::invokeMethod(
            owner,
            [owner, id, callback, result]() { owner->deliver(id, callback, result); },
            Qt::QueuedConnection);
    }

    Recognizer *m_owner = nullptr;
    QThread *m_thread = nullptr;
    QMutex m_mutex;
    QWaitCondition m_wake;
    std::deque<Job> m_queue;
    QVector<RequestId> m_cancelled;
    bool m_quitting = false;
};

Recognizer::Recognizer(QObject *parent)
    : QObject(parent)
{
}

Recognizer::~Recognizer()
{
    // 先停线程，再走 QObject 析构：尚未派发的 queued 回调会随对象析构被回收，
    // 不会打到已经销毁的捕获对象上。
    delete m_worker;
    m_worker = nullptr;
}

void Recognizer::ensureWorker()
{
    if (m_worker)
        return;
    m_worker = new Worker(this);
    m_worker->start();
}

void Recognizer::probe()
{
    ensureWorker();
}

void Recognizer::applyState(bool ready, const QString &description, const QString &reason)
{
    m_state = ready ? State::Ready : State::Unavailable;
    m_description = ready ? description : QString();
    m_unavailableReason = ready ? QString() : reason;
    emit stateChanged(m_state);
}

Recognizer::RequestId Recognizer::recognize(const QImage &image, Callback callback)
{
    const RequestId id = m_nextId++;
    if (!callback)
        return id;

    ensureWorker();
    m_pending.append(id);

    if (image.isNull()) {
        // 空图也走同一条异步通道，调用方不需要区分同步/异步两条路径。
        QMetaObject::invokeMethod(
            this,
            [this, id, callback]() {
                Result result;
                result.error = tr("选区截图为空。");
                deliver(id, callback, result);
            },
            Qt::QueuedConnection);
        return id;
    }

    m_worker->submit(id, image, std::move(callback));
    return id;
}

void Recognizer::cancel(RequestId id)
{
    if (!m_pending.contains(id))
        return;
    m_cancelled.append(id);
    if (m_worker)
        m_worker->cancel(id);
}

void Recognizer::deliver(RequestId id, const Callback &callback, const Result &result)
{
    m_pending.removeAll(id);
    if (m_cancelled.removeAll(id) > 0)
        return;   // 已取消：结果丢弃，回调不执行
    if (callback)
        callback(result);
}

} // namespace Ocr
