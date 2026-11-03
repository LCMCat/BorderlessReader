#include "plugins/DemoPlugin.h"

#include "core/Utils/ScreenUtils.h"

#include <QDialog>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
QIcon makeDemoIcon()
{
    QIcon icon;

    for (const int size : {32, 48, 64, 96}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal s = static_cast<qreal>(size);
        QPen framePen(QColor(0x25, 0x32, 0x46));
        framePen.setWidthF(std::max(1.5, s * 0.065));
        framePen.setCapStyle(Qt::RoundCap);
        framePen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(framePen);

        // 四个角框 + 一条扫描线，表达“框选截图”的工具含义。
        const QRectF frame(s * 0.22, s * 0.25, s * 0.56, s * 0.50);
        const qreal corner = s * 0.16;
        painter.drawLine(frame.topLeft(), frame.topLeft() + QPointF(corner, 0));
        painter.drawLine(frame.topLeft(), frame.topLeft() + QPointF(0, corner));
        painter.drawLine(frame.topRight(), frame.topRight() - QPointF(corner, 0));
        painter.drawLine(frame.topRight(), frame.topRight() + QPointF(0, corner));
        painter.drawLine(frame.bottomLeft(), frame.bottomLeft() + QPointF(corner, 0));
        painter.drawLine(frame.bottomLeft(), frame.bottomLeft() - QPointF(0, corner));
        painter.drawLine(frame.bottomRight(), frame.bottomRight() - QPointF(corner, 0));
        painter.drawLine(frame.bottomRight(), frame.bottomRight() - QPointF(0, corner));

        QPen accentPen(QColor(0xC9, 0x9A, 0x3D));
        accentPen.setWidthF(std::max(1.2, s * 0.052));
        accentPen.setCapStyle(Qt::RoundCap);
        painter.setPen(accentPen);
        painter.drawLine(QPointF(s * 0.37, s * 0.50), QPointF(s * 0.63, s * 0.50));

        // 右下角的四角星小徽标。
        QPainterPath star;
        const QPointF center(s * 0.76, s * 0.70);
        const qreal radius = s * 0.075;
        star.moveTo(center.x(), center.y() - radius);
        star.quadTo(center.x() + radius * 0.18, center.y() - radius * 0.18,
                    center.x() + radius, center.y());
        star.quadTo(center.x() + radius * 0.18, center.y() + radius * 0.18,
                    center.x(), center.y() + radius);
        star.quadTo(center.x() - radius * 0.18, center.y() + radius * 0.18,
                    center.x() - radius, center.y());
        star.quadTo(center.x() - radius * 0.18, center.y() - radius * 0.18,
                    center.x(), center.y() - radius);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0xC9, 0x9A, 0x3D));
        painter.drawPath(star);

        painter.end();
        icon.addPixmap(pixmap);
    }

    return icon;
}

QPixmap makePreviewPixmap(const QPixmap &source, const QSizeF &maximumLogicalSize)
{
    if (source.isNull())
        return {};

    const qreal dpr = source.devicePixelRatio();
    const QSizeF logical = source.deviceIndependentSize();
    if (logical.width() <= 0.0 || logical.height() <= 0.0 || dpr <= 0.0)
        return source;

    const qreal factor = std::min({
        1.0,
        maximumLogicalSize.width() / logical.width(),
        maximumLogicalSize.height() / logical.height(),
    });

    if (factor >= 1.0)
        return source;

    const QSize targetPixels(std::max(1, qRound(logical.width() * factor * dpr)),
                             std::max(1, qRound(logical.height() * factor * dpr)));
    QPixmap scaled = source.scaled(targetPixels, Qt::IgnoreAspectRatio,
                                   Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    return scaled;
}

QLabel *makeKeyLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("infoKey"));
    return label;
}

QLabel *makeValueLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("infoValue"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QFrame *makeInfoBlock(const QString &key, const QString &value, QWidget *parent)
{
    auto *block = new QFrame(parent);
    block->setObjectName(QStringLiteral("infoBlock"));

    auto *layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(makeKeyLabel(key, block));
    layout->addWidget(makeValueLabel(value, block));
    return block;
}
} // namespace

// ---------------------------------------------------------------------------
// 结果窗口：用普通 QDialog，方便小白直接看懂并替换成自己的业务界面。
// ---------------------------------------------------------------------------
class DemoResultDialog final : public QDialog
{
public:
    DemoResultDialog(const QRect &globalRect,
                     const QPixmap &regionPixmap,
                     const QIcon &windowIcon,
                     QWidget *parent)
        : QDialog(parent)
    {
        setObjectName(QStringLiteral("DemoResultDialog"));
        setWindowTitle(tr("选区结果 · DemoPlugin"));
        setWindowIcon(windowIcon);
        setAttribute(Qt::WA_DeleteOnClose, true);
        setMinimumWidth(620);
        setMaximumWidth(620);

        setStyleSheet(QStringLiteral(
            "QDialog#DemoResultDialog {"
            "  background: #F6F2E9;"
            "}"
            "QLabel {"
            "  font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Segoe UI';"
            "  color: #202A39;"
            "}"
            "QLabel#eyebrow {"
            "  color: #9A7A38;"
            "  font-size: 10px;"
            "  font-weight: 600;"
            "}"
            "QLabel#title {"
            "  color: #1D2736;"
            "  font-size: 23px;"
            "  font-weight: 600;"
            "}"
            "QLabel#subtitle {"
            "  color: #7B818A;"
            "  font-size: 11px;"
            "}"
            "QLabel#chip {"
            "  color: #654B17;"
            "  background: #F0E0B7;"
            "  border: 1px solid #E5D19E;"
            "  border-radius: 9px;"
            "  padding: 4px 9px;"
            "  font-size: 9px;"
            "  font-weight: 600;"
            "}"
            "QFrame#previewCard {"
            "  background: #18212E;"
            "  border-radius: 16px;"
            "}"
            "QLabel#previewMeta {"
            "  color: #AAB6C8;"
            "  font-size: 9px;"
            "  font-weight: 600;"
            "}"
            "QLabel#previewDot {"
            "  color: #D7AF5A;"
            "  font-size: 12px;"
            "}"
            "QLabel#preview {"
            "  background: #111924;"
            "  border: 1px solid #2A374A;"
            "  border-radius: 10px;"
            "  color: #93A0B3;"
            "  font-size: 11px;"
            "}"
            "QFrame#infoCard {"
            "  background: #FFFDF8;"
            "  border: 1px solid #E6DFD1;"
            "  border-radius: 14px;"
            "}"
            "QFrame#ocrCard {"
            "  background: #FFFDF8;"
            "  border: 1px solid #E6DFD1;"
            "  border-radius: 14px;"
            "}"
            "QLabel#ocrKey {"
            "  color: #8A877E;"
            "  font-size: 10px;"
            "}"
            "QLabel#ocrText {"
            "  color: #1D2736;"
            "  font-size: 15px;"
            "  font-weight: 600;"
            "}"
            "QLabel#ocrMeta {"
            "  color: #8A877E;"
            "  font-size: 10px;"
            "}"
            "QLabel#infoKey {"
            "  color: #8A877E;"
            "  font-size: 10px;"
            "}"
            "QLabel#infoValue {"
            "  color: #222C3B;"
            "  font-family: 'Cascadia Mono', 'Consolas', monospace;"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "}"
            "QLabel#infoNote {"
            "  color: #99958B;"
            "  font-size: 9px;"
            "}"
            "QFrame#divider {"
            "  background: #ECE6DA;"
            "  min-height: 1px;"
            "  max-height: 1px;"
            "}"
            "QLabel#hint {"
            "  color: #A19B8F;"
            "  font-size: 10px;"
            "}"
            "QPushButton#closeButton {"
            "  color: #FFF8EA;"
            "  background: #1F2B3C;"
            "  border: none;"
            "  border-radius: 10px;"
            "  padding: 9px 24px;"
            "  font-size: 12px;"
            "  font-weight: 600;"
            "}"
            "QPushButton#closeButton:hover {"
            "  background: #2D3E56;"
            "}"
            "QPushButton#closeButton:pressed {"
            "  background: #17212E;"
            "}"
        ));

        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(26, 22, 26, 20);
        outer->setSpacing(14);

        // 顶栏：图标、标题、Demo 标识。
        auto *header = new QHBoxLayout;
        header->setSpacing(12);

        auto *iconTile = new QLabel(this);
        iconTile->setFixedSize(46, 46);
        iconTile->setAlignment(Qt::AlignCenter);
        iconTile->setPixmap(windowIcon.pixmap(26, 26));
        iconTile->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background: #FFFFFF;"
            "  border: 1px solid #E8E1D3;"
            "  border-radius: 14px;"
            "}"
        ));
        header->addWidget(iconTile, 0, Qt::AlignVCenter);

        auto *titleColumn = new QVBoxLayout;
        titleColumn->setContentsMargins(0, 0, 0, 0);
        titleColumn->setSpacing(2);

        auto *eyebrow = new QLabel(tr("CAPTURE CALLBACK"), this);
        eyebrow->setObjectName(QStringLiteral("eyebrow"));
        auto *title = new QLabel(tr("选区已送达"), this);
        title->setObjectName(QStringLiteral("title"));
        auto *subtitle = new QLabel(tr("DemoPlugin 已收到全局坐标与 HiDPI 截图"), this);
        subtitle->setObjectName(QStringLiteral("subtitle"));

        titleColumn->addWidget(eyebrow);
        titleColumn->addWidget(title);
        titleColumn->addWidget(subtitle);
        header->addLayout(titleColumn, 1);

        auto *chip = new QLabel(tr("DEMO PLUGIN"), this);
        chip->setObjectName(QStringLiteral("chip"));
        header->addWidget(chip, 0, Qt::AlignTop);
        outer->addLayout(header);

        // 截图预览。
        auto *previewCard = new QFrame(this);
        previewCard->setObjectName(QStringLiteral("previewCard"));
        auto *previewLayout = new QVBoxLayout(previewCard);
        previewLayout->setContentsMargins(12, 10, 12, 12);
        previewLayout->setSpacing(7);

        auto *previewMetaRow = new QHBoxLayout;
        previewMetaRow->setSpacing(5);
        auto *previewDot = new QLabel(QStringLiteral("●"), previewCard);
        previewDot->setObjectName(QStringLiteral("previewDot"));
        auto *previewMeta = new QLabel(tr("CAPTURED REGION"), previewCard);
        previewMeta->setObjectName(QStringLiteral("previewMeta"));
        previewMetaRow->addWidget(previewDot, 0, Qt::AlignVCenter);
        previewMetaRow->addWidget(previewMeta, 0, Qt::AlignVCenter);
        previewMetaRow->addStretch(1);

        auto *pixelMeta = new QLabel(
            tr("%1 × %2 px").arg(regionPixmap.width()).arg(regionPixmap.height()),
            previewCard);
        pixelMeta->setObjectName(QStringLiteral("previewMeta"));
        previewMetaRow->addWidget(pixelMeta, 0, Qt::AlignVCenter);
        previewLayout->addLayout(previewMetaRow);

        auto *preview = new QLabel(previewCard);
        preview->setObjectName(QStringLiteral("preview"));
        preview->setFixedHeight(208);
        preview->setAlignment(Qt::AlignCenter);
        preview->setScaledContents(false);
        preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

        if (regionPixmap.isNull()) {
            preview->setText(tr("没有可用截图"));
        } else {
            preview->setPixmap(makePreviewPixmap(regionPixmap, QSizeF(520.0, 196.0)));
            preview->setToolTip(tr("原始截图：%1 × %2 物理像素，DPR %3")
                                    .arg(regionPixmap.width())
                                    .arg(regionPixmap.height())
                                    .arg(QString::number(regionPixmap.devicePixelRatio(), 'f', 2)));
        }
        previewLayout->addWidget(preview);
        outer->addWidget(previewCard);

        // OCR 结果：先占位，识别完成后由 setOcrResult() 填进来。
        auto *ocrCard = new QFrame(this);
        ocrCard->setObjectName(QStringLiteral("ocrCard"));
        auto *ocrLayout = new QVBoxLayout(ocrCard);
        ocrLayout->setContentsMargins(18, 12, 18, 12);
        ocrLayout->setSpacing(5);

        auto *ocrKey = new QLabel(tr("选区文字 · core/Ocr"), ocrCard);
        ocrKey->setObjectName(QStringLiteral("ocrKey"));
        ocrLayout->addWidget(ocrKey);

        m_ocrText = new QLabel(tr("识别中…"), ocrCard);
        m_ocrText->setObjectName(QStringLiteral("ocrText"));
        m_ocrText->setWordWrap(true);
        m_ocrText->setMinimumHeight(46);   // 预留两行，结果回来时窗口不会突然跳高
        m_ocrText->setTextInteractionFlags(Qt::TextSelectableByMouse);
        ocrLayout->addWidget(m_ocrText);

        m_ocrMeta = new QLabel(tr("正在识别选区…"), ocrCard);
        m_ocrMeta->setObjectName(QStringLiteral("ocrMeta"));
        m_ocrMeta->setWordWrap(true);
        ocrLayout->addWidget(m_ocrMeta);

        outer->addWidget(ocrCard);

        // 数据结构说明。
        auto *infoCard = new QFrame(this);
        infoCard->setObjectName(QStringLiteral("infoCard"));
        auto *infoLayout = new QVBoxLayout(infoCard);
        infoLayout->setContentsMargins(18, 14, 18, 12);
        infoLayout->setSpacing(10);

        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(28);
        grid->setVerticalSpacing(11);

        const QString coordinateText = tr("X %1    Y %2")
                                            .arg(globalRect.x())
                                            .arg(globalRect.y());
        const QString sizeText = tr("%1 × %2  DIP")
                                     .arg(globalRect.width())
                                     .arg(globalRect.height());
        const QString pixelText = tr("%1 × %2  px")
                                      .arg(regionPixmap.width())
                                      .arg(regionPixmap.height());
        const QString dprText = QStringLiteral("%1×")
                                    .arg(QString::number(regionPixmap.devicePixelRatio(), 'f', 2));

        grid->addWidget(makeInfoBlock(tr("全局坐标"), coordinateText, infoCard), 0, 0);
        grid->addWidget(makeInfoBlock(tr("选区尺寸"), sizeText, infoCard), 0, 1);
        grid->addWidget(makeInfoBlock(tr("截图像素"), pixelText, infoCard), 1, 0);
        grid->addWidget(makeInfoBlock(tr("设备像素比"), dprText, infoCard), 1, 1);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(1, 1);
        infoLayout->addLayout(grid);

        auto *divider = new QFrame(infoCard);
        divider->setObjectName(QStringLiteral("divider"));
        infoLayout->addWidget(divider);

        auto *note = new QLabel(
            tr("全局坐标单位为 Qt 逻辑像素（DIP）；截图是真实物理像素，"
               "两者比例即 devicePixelRatio。"),
            infoCard);
        note->setObjectName(QStringLiteral("infoNote"));
        note->setWordWrap(true);
        infoLayout->addWidget(note);
        outer->addWidget(infoCard);

        // 底部操作。
        auto *footer = new QHBoxLayout;
        auto *hint = new QLabel(tr("Esc 关闭窗口"), this);
        hint->setObjectName(QStringLiteral("hint"));
        footer->addWidget(hint, 0, Qt::AlignVCenter);
        footer->addStretch(1);

        auto *closeButton = new QPushButton(tr("关闭"), this);
        closeButton->setObjectName(QStringLiteral("closeButton"));
        closeButton->setCursor(Qt::PointingHandCursor);
        closeButton->setAutoDefault(false);
        closeButton->setDefault(false);
        connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
        footer->addWidget(closeButton, 0, Qt::AlignVCenter);
        outer->addLayout(footer);

        setFixedWidth(620);
        adjustSize();
    }

    /// 识别结果回来后填进窗口；识别中窗口已经可见，这里只更新文字。
    void setOcrResult(bool ready,
                      const QString &backend,
                      const QString &reason,
                      const Ocr::Result &result)
    {
        if (!result.isOk()) {
            m_ocrText->setText(tr("识别不可用"));
            m_ocrMeta->setText(reason.isEmpty() ? result.error : reason);
        } else if (result.words.isEmpty()) {
            m_ocrText->setText(tr("没有识别到文字"));
            m_ocrMeta->setText(ready ? backend : reason);
        } else {
            // 只展示前若干个词，避免把窗口撑成一段长文；词框信息证明坐标是真实可用的。
            constexpr int kMaxShownWords = 12;
            const int shown = qMin(static_cast<int>(result.words.size()), kMaxShownWords);
            QStringList words;
            words.reserve(shown);
            for (int i = 0; i < shown; ++i)
                words.append(result.words.at(i).text);
            if (static_cast<int>(result.words.size()) > shown)
                words.append(QStringLiteral("…"));
            m_ocrText->setText(words.join(QLatin1Char(' ')));

            const Ocr::Word &first = result.words.first();
            m_ocrMeta->setText(tr("%1 · %2 个词 · 首词框 [%3, %4, %5 × %6]")
                                   .arg(backend)
                                   .arg(static_cast<int>(result.words.size()))
                                   .arg(qRound(first.box.x()))
                                   .arg(qRound(first.box.y()))
                                   .arg(qRound(first.box.width()))
                                   .arg(qRound(first.box.height())));
        }

        // 文字变长会把窗口撑高：重新收一下尺寸，再保证不越出当前屏幕。
        adjustSize();
        QScreen *screen = ScreenUtils::screenForRect(frameGeometry());
        if (screen)
            move(ScreenUtils::clampWindowToAvailableGeometry(frameGeometry(), screen).topLeft());
    }

private:
    QLabel *m_ocrText = nullptr;
    QLabel *m_ocrMeta = nullptr;
};

QString DemoPlugin::name() const
{
    // IToolPlugin 不是 QObject，不能直接调用 tr()。若以后需要多语言，
    // 可以在这里使用 QCoreApplication::translate("DemoPlugin", ...)。
    return QStringLiteral("选区演示");
}

QIcon DemoPlugin::icon() const
{
    static const QIcon demoIcon = makeDemoIcon();
    return demoIcon;
}

void DemoPlugin::onSelection(const Selection &selection)
{
    const QRect &globalRect = selection.globalRect;
    const QPixmap &regionPixmap = selection.regionPixmap;

    // 每次选区都可以生成新的结果窗口；旧窗口会被关闭，避免窗口越开越多。
    if (m_resultDialog)
        m_resultDialog->close();

    auto *dialog = new DemoResultDialog(globalRect, regionPixmap, icon(), nullptr);
    m_resultDialog = dialog;

    QScreen *screen = ScreenUtils::screenForRect(globalRect);
    if (screen) {
        const QRect available = screen->availableGeometry();
        QRect target(QPoint(0, 0), dialog->size());
        target.moveCenter(available.center());
        dialog->move(ScreenUtils::clampWindowToAvailableGeometry(target, screen).topLeft());
    }

    dialog->show();
    dialog->raise();
    dialog->activateWindow();

    // OCR 是框架机制：插件只发起请求，结果回到 GUI 线程后再填进已经打开的窗口。
    // 上一次识别如果还没回来就取消，避免旧结果盖到新窗口上。
    m_recognizer.cancel(m_ocrRequest);
    const QPointer<DemoResultDialog> guard(dialog);
    m_ocrRequest = m_recognizer.recognize(
        regionPixmap.toImage(),
        [this, guard](const Ocr::Result &result) {
            if (!guard)
                return;   // 窗口已经关了，这次结果没人要
            guard->setOcrResult(m_recognizer.state() == Ocr::Recognizer::State::Ready,
                                m_recognizer.backendDescription(),
                                m_recognizer.unavailableReason(),
                                result);
        });
}
