#include "plugins/WordLookup/WordLookupPlugin.h"

#include "core/Utils/CaptureUtils.h"
#include "plugins/WordLookup/LookupDialog.h"
#include "plugins/WordLookup/WordPicker.h"
#include "plugins/WordLookup/lookup/LookupService.h"

#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

#include <algorithm>

namespace
{
QIcon makeLookupIcon()
{
    QIcon icon;

    for (const int size : { 32, 48, 64, 96 }) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal s = static_cast<qreal>(size);

        // 一张纸页，表示原文。
        QPen pagePen(QColor(0x25, 0x32, 0x46));
        pagePen.setWidthF(std::max(1.4, s * 0.058));
        painter.setPen(pagePen);
        painter.setBrush(Qt::NoBrush);
        const QRectF page(s * 0.16, s * 0.15, s * 0.44, s * 0.58);
        painter.drawRoundedRect(page, s * 0.07, s * 0.07);

        // 纸上的三行正文。
        QPen linePen(QColor(0x9A, 0xA8, 0xBC));
        linePen.setWidthF(std::max(1.0, s * 0.042));
        linePen.setCapStyle(Qt::RoundCap);
        painter.setPen(linePen);
        for (int i = 0; i < 3; ++i) {
            const qreal y = page.top() + s * (0.14 + 0.13 * i);
            painter.drawLine(QPointF(page.left() + s * 0.08, y),
                             QPointF(page.right() - s * 0.08, y));
        }

        // 一把放大镜，压在纸的右下角，表示“查”。
        QPen lensPen(QColor(0xC9, 0x9A, 0x3D));
        lensPen.setWidthF(std::max(1.5, s * 0.072));
        lensPen.setCapStyle(Qt::RoundCap);
        painter.setPen(lensPen);
        painter.setBrush(Qt::NoBrush);
        const QPointF lensCenter(s * 0.68, s * 0.63);
        const qreal lensRadius = s * 0.155;
        painter.drawEllipse(lensCenter, lensRadius, lensRadius);
        painter.drawLine(QPointF(lensCenter.x() + lensRadius * 0.71, lensCenter.y() + lensRadius * 0.71),
                         QPointF(s * 0.90, s * 0.87));

        painter.end();
        icon.addPixmap(pixmap);
    }

    return icon;
}
} // namespace

WordLookupPlugin::WordLookupPlugin() = default;

WordLookupPlugin::~WordLookupPlugin() = default;

QString WordLookupPlugin::name() const
{
    return QStringLiteral("查词");
}

QIcon WordLookupPlugin::icon() const
{
    static const QIcon lookupIcon = makeLookupIcon();
    return lookupIcon;
}

void WordLookupPlugin::ensureService()
{
    if (m_serviceChecked)
        return;
    m_serviceChecked = true;

    m_service = std::make_unique<LookupService>();
    QString error;
    if (!m_service->initialize(&error))
        m_serviceError = error;
}

void WordLookupPlugin::openDialog(const QRect &anchor)
{
    if (m_dialog)
        m_dialog->close();

    auto *dialog = new LookupDialog(QString(), icon(), m_history, false, nullptr);
    m_dialog = dialog;

    // 用户手动输入/点历史词时，由插件负责查，而不是窗口自己查。
    dialog->setLookupHandler([this](const QString &word) { requestLookup(word, false); });

    dialog->placeNextTo(anchor);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    dialog->settleWordField();
    dialog->enableAutoHide();
}

void WordLookupPlugin::rememberWord(const QString &word)
{
    const QString normalized = word.trimmed().toLower();
    if (normalized.isEmpty())
        return;

    m_history.removeAll(normalized);
    m_history.prepend(normalized);
    while (m_history.size() > 5)
        m_history.removeLast();
}

void WordLookupPlugin::requestLookup(const QString &word, bool uncertain)
{
    if (!m_dialog)
        return;

    m_dialog->setUncertain(uncertain);
    m_dialog->setWord(word);
    rememberWord(word);

    if (!m_service || !m_service->isReady()) {
        m_dialog->showMessage(m_serviceError.isEmpty()
                                  ? QStringLiteral("词库尚未加载。")
                                  : m_serviceError);
        return;
    }

    const LookupResult result = m_service->lookup(word);
    if (!result.found()) {
        m_dialog->showMessage(result.message);
        return;
    }

    m_dialog->showEntry(result.entry, result.matchedFrom);
}

void WordLookupPlugin::startRecognition(const Selection &selection)
{
    // 优先用整屏快照里的一块上下文做 OCR：单个词单独裁出来识别率很差，
    // 整行/整段一起识别再按词框取词才稳。没有整屏快照时退回选区截图。
    QImage image;
    QRectF selectionInImage;

    if (!selection.desktopImage.isNull() && selection.desktopScale > 0.0
        && selection.desktopRect.isValid()) {
        DesktopCapture capture;
        capture.globalRect = selection.desktopRect;
        capture.image = selection.desktopImage;
        capture.scale = selection.desktopScale;

        const int bandX = std::max(240, selection.globalRect.width() * 3);
        const int bandY = std::max(110, selection.globalRect.height() * 4);
        const QRect contextGlobal = selection.globalRect
                                        .adjusted(-bandX, -bandY, bandX, bandY)
                                        .intersected(selection.desktopRect);
        if (contextGlobal.isValid()) {
            const QRect contextPixels = CaptureUtils::mapToCapturePixels(capture, contextGlobal);
            if (contextPixels.isValid()) {
                image = selection.desktopImage.copy(contextPixels);
                const QRect selectionPixels =
                    CaptureUtils::mapToCapturePixels(capture, selection.globalRect);
                selectionInImage = QRectF(selectionPixels.translated(-contextPixels.topLeft()));
            }
        }
    }

    if (image.isNull()) {
        image = selection.regionPixmap.toImage();
        selectionInImage = QRectF(image.rect());
    }

    m_recognizer.cancel(m_ocrRequest);
    m_ocrRequest = m_recognizer.recognize(
        image,
        [this, selectionInImage](const Ocr::Result &result) {
            if (!m_dialog)
                return;

            if (!result.isOk()) {
                m_dialog->showMessage(result.error);
                return;
            }

            const WordPicker::PickedWord picked =
                WordPicker::pick(result.words, selectionInImage);
            if (picked.text.isEmpty()) {
                m_dialog->showMessage(QStringLiteral("没有识别到文字，可直接输入单词后回车。"));
                return;
            }

            requestLookup(picked.text, !picked.certain);
        });
}

void WordLookupPlugin::onSelection(const Selection &selection)
{
    openDialog(selection.globalRect);

    // 先让窗口出来，再做会阻塞的初始化；否则用户点完卡片要愣一下才看到界面。
    if (m_dialog)
        m_dialog->showMessage(QStringLiteral("正在加载词库…"));

    ensureService();

    if (!m_service || !m_service->isReady()) {
        if (m_dialog)
            m_dialog->showMessage(m_serviceError.isEmpty()
                                      ? QStringLiteral("词库尚未加载。")
                                      : m_serviceError);
        return;
    }

    if (m_dialog)
        m_dialog->showMessage(QStringLiteral("正在识别选区…"));

    startRecognition(selection);
}
