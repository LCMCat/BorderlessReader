#pragma once

#include <QImage>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QVector>

#include <functional>

namespace Ocr
{
/// OCR 识别出的一个词及其在图片里的位置。
struct Word
{
    QString text;
    /// 像素坐标，原点在传入 QImage 的左上角。若图片带 devicePixelRatio，
    /// 这里仍然是真实像素，不是 DIP——需要 DIP 时由调用方自己换算。
    QRectF box;
};

/// 一次识别的结果。
struct Result
{
    QVector<Word> words;
    /// 为空表示识别流程成功；成功但图里没有文字时 words 为空、error 也为空。
    QString error;

    bool isOk() const { return error.isEmpty(); }
};

/**
 * @brief OCR 服务：把一张图变成“文字 + 词框”。
 *
 * 这是**机制**，不是业务。它只回答“图上有什么字”，不做这些决定：
 *  - 框松了该保留哪个词（选区取舍是插件的策略）；
 *  - 认错了要不要用词库纠正（纠错是业务策略）；
 *  - 识别前要不要放大、二值化（预处理属于调用方，输什么图就认什么图）。
 *
 * 线程约定：
 *  - 所有公开方法只能在 GUI 线程调用；
 *  - 回调保证在 GUI 线程执行，因此可以安全地碰控件；
 *  - 内部只有一个工作线程，请求串行处理；不会每识别一次就新建线程。
 *
 * 生命周期：
 *  - 一个实例持有自己的后端和工作线程，通常由需要 OCR 的插件持有；
 *  - 析构会等工作线程结束。Windows OCR 是同步调用、无法中途打断，
 *    所以正在进行的那一次识别会跑完（通常几十到几百毫秒）。
 *  - 回调由本对象派发：本对象销毁后，尚未派发的回调会被丢弃，
 *    不会打到已经析构的捕获对象上。
 */
class Recognizer final : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Unknown,      ///< 还没有启动/探测
        Ready,        ///< 后端可用
        Unavailable,  ///< 没有编译后端，或初始化失败
    };
    Q_ENUM(State)

    using Callback = std::function<void(const Result &)>;
    using RequestId = quint64;

    explicit Recognizer(QObject *parent = nullptr);
    ~Recognizer() override;

    State state() const { return m_state; }
    /// 后端描述，例如 "Windows.Media.Ocr · en-US"；不可用时为空。
    QString backendDescription() const { return m_description; }
    /// 不可用的原因；可用时为空。
    QString unavailableReason() const { return m_unavailableReason; }

    /**
     * @brief 异步探测后端。
     *
     * 启动工作线程并初始化后端，结果通过 stateChanged() 通知。初始化在
     * 工作线程完成，不会阻塞 GUI。已经探测过时直接返回。
     */
    void probe();

    /**
     * @brief 异步识别一张图。
     *
     * @param image    要识别的图。按值传入，调用方之后可以随意释放 QPixmap。
     * @param callback 结果回调，在 GUI 线程执行。
     * @return 请求号，可用于 cancel()。
     */
    RequestId recognize(const QImage &image, Callback callback);

    /// 丢弃尚未开始或尚未派发的请求。已经开始的那次识别无法中断。
    void cancel(RequestId id);

signals:
    void stateChanged(Ocr::Recognizer::State state);

private:
    class Worker;

    void ensureWorker();
    void applyState(bool ready, const QString &description, const QString &reason);
    void deliver(RequestId id, const Callback &callback, const Result &result);

    Worker *m_worker = nullptr;
    State m_state = State::Unknown;
    QString m_description;
    QString m_unavailableReason;
    RequestId m_nextId = 1;
    QVector<RequestId> m_pending;
    QVector<RequestId> m_cancelled;
};
} // namespace Ocr
