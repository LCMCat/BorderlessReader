#include "core/RegionSelector.h"

#include "core/Utils/ScreenUtils.h"

#include <QCloseEvent>
#include <QCursor>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kMinimumSelectionSize = 4;
const QColor kAccent(0xE6, 0xC8, 0x86);
const QColor kSelectionGold(0xC8, 0x98, 0x3D);

QRect selectionRectBetween(const QPoint &a, const QPoint &b)
{
    // 使用“两点的差”作为宽高，而不是 QRect(point1, point2) 的包含式宽高。
    // 这样从 (100, 100) 拖到 (300, 300) 得到 200×200，符合直觉。
    const int x = std::min(a.x(), b.x());
    const int y = std::min(a.y(), b.y());
    const int width = std::abs(a.x() - b.x());
    const int height = std::abs(a.y() - b.y());
    return QRect(x, y, width, height);
}
} // namespace

RegionSelector::RegionSelector(QWidget *parent)
    : QWidget(parent)
{
    // 窗口标志必须一次性按位或设置。分多次 setWindowFlags() 会让 Qt 重建
    // 原生窗口，容易丢失 WindowStaysOnTopHint 或出现闪烁。
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::Tool
                   | Qt::WindowStaysOnTopHint
                   | Qt::NoDropShadowWindowHint);

    setWindowTitle(tr("阅界 - 区域选择"));
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::CrossCursor);
}

RegionSelector::~RegionSelector()
{
    releaseInputGrab();
}

bool RegionSelector::beginSelection()
{
    if (m_finished)
        return false;

    m_capture = CaptureUtils::captureVirtualDesktop();
    if (!m_capture.isValid())
        return false;

    m_virtualRect = m_capture.globalRect;

    m_colorPixmap = QPixmap::fromImage(m_capture.image);
    m_colorPixmap.setDevicePixelRatio(1.0);

    // 先做灰度化，再压一层冷色半透明。选区内部会重新绘制彩色截图，
    // 这样“屏幕变灰、选区恢复原色”的效果不依赖鼠标区域擦除等技巧。
    QImage gray = m_capture.image.convertToFormat(QImage::Format_Grayscale8);
    gray = gray.convertToFormat(QImage::Format_RGB32);
    {
        QPainter grayPainter(&gray);
        grayPainter.fillRect(gray.rect(), QColor(25, 31, 41, 100));
    }
    m_dimmedPixmap = QPixmap::fromImage(gray);
    m_dimmedPixmap.setDevicePixelRatio(1.0);

    // 多屏时把顶部提示居中到“鼠标当前所在屏幕”，而不是所有屏幕组成的
    // 超宽虚拟桌面的中心，避免提示落在两块屏幕的接缝处。
    QScreen *activeScreen = ScreenUtils::screenAtPoint(QCursor::pos());
    m_instructionArea = activeScreen
        ? activeScreen->geometry().translated(-m_virtualRect.topLeft())
        : QRect(QPoint(0, 0), m_virtualRect.size());
    m_instructionArea = m_instructionArea.intersected(
        QRect(QPoint(0, 0), m_virtualRect.size()));

    setGeometry(m_virtualRect);
    m_cursorGlobal = clampToVirtualDesktop(QCursor::pos());
    m_hasCursor = true;
    m_selecting = false;
    m_finished = false;

    show();
    raise();
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);

    // Qt::Tool + 无边框窗口在部分 Windows 合成场景中第一次 activateWindow()
    // 可能拿不到键盘焦点；下一轮事件循环再拿一次，保证 Esc 一定能取消。
    QTimer::singleShot(0, this, [this]() {
        if (m_finished)
            return;
        raise();
        activateWindow();
        setFocus(Qt::ActiveWindowFocusReason);
        grabKeyboard();
    });

    update();
    return true;
}

QRect RegionSelector::currentSelection() const
{
    if (!m_selecting)
        return {};

    const QRect raw = selectionRectBetween(m_anchorGlobal, m_cursorGlobal);
    if (raw.width() <= 0 || raw.height() <= 0)
        return {};

    return ScreenUtils::clampSelectionToVirtualDesktop(raw);
}

QPoint RegionSelector::clampToVirtualDesktop(const QPoint &globalPoint) const
{
    if (!m_virtualRect.isValid())
        return globalPoint;

    return QPoint(std::clamp(globalPoint.x(), m_virtualRect.left(), m_virtualRect.right()),
                  std::clamp(globalPoint.y(), m_virtualRect.top(), m_virtualRect.bottom()));
}

void RegionSelector::finishWithSelection()
{
    const QRect region = currentSelection();
    if (!region.isValid()
        || region.width() < kMinimumSelectionSize
        || region.height() < kMinimumSelectionSize) {
        // 只是点了一下而没有真正拖出区域，视为取消，避免发出空截图。
        cancelSelection();
        return;
    }

    const QPixmap regionPixmap = CaptureUtils::cropRegion(m_capture, region);
    if (regionPixmap.isNull()) {
        cancelSelection();
        return;
    }

    m_finished = true;
    m_selecting = false;
    releaseInputGrab();
    hide();

    // 整屏快照一并交给插件：QImage 隐式共享，这里只是多一个引用，
    // 插件要做“整行 OCR”时不必自己重新抓屏。
    Selection selection;
    selection.globalRect = region;
    selection.regionPixmap = regionPixmap;
    selection.desktopImage = m_capture.image;
    selection.desktopRect = m_capture.globalRect;
    selection.desktopScale = m_capture.scale;

    // 先隐藏选区器，再同步通知插件，Demo 窗口出现时不会被遮罩挡在后面。
    emit selectionReady(selection);
    deleteLater();
}

void RegionSelector::cancelSelection()
{
    if (m_finished)
        return;

    m_finished = true;
    m_selecting = false;
    releaseInputGrab();
    hide();

    emit canceled();
    deleteLater();
}

void RegionSelector::releaseInputGrab()
{
    if (QWidget::mouseGrabber() == this)
        releaseMouse();
    if (QWidget::keyboardGrabber() == this)
        releaseKeyboard();
}

void RegionSelector::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        cancelSelection();
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    m_anchorGlobal = clampToVirtualDesktop(event->globalPosition().toPoint());
    m_cursorGlobal = m_anchorGlobal;
    m_selecting = true;
    m_hasCursor = true;

    // 拖拽过程中鼠标仍归选区器所有，松手事件不会漏给桌面。
    grabMouse();
    update();
    event->accept();
}

void RegionSelector::mouseMoveEvent(QMouseEvent *event)
{
    m_cursorGlobal = clampToVirtualDesktop(event->globalPosition().toPoint());
    m_hasCursor = true;
    update();
    event->accept();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_selecting) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    m_cursorGlobal = clampToVirtualDesktop(event->globalPosition().toPoint());
    finishWithSelection();
    event->accept();
}

void RegionSelector::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelSelection();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void RegionSelector::closeEvent(QCloseEvent *event)
{
    if (!m_finished) {
        m_finished = true;
        m_selecting = false;
        releaseInputGrab();
        emit canceled();
        deleteLater();
    }

    QWidget::closeEvent(event);
}

void RegionSelector::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QRect desktopRect(QPoint(0, 0), m_virtualRect.size());
    if (m_dimmedPixmap.isNull()) {
        painter.fillRect(rect(), QColor(18, 22, 29));
    } else {
        painter.drawPixmap(desktopRect, m_dimmedPixmap);
    }

    const QRect selection = currentSelection();
    if (selection.width() > 0 && selection.height() > 0) {
        const QRect localSelection = selection.translated(-m_virtualRect.topLeft());
        const QRect sourcePixels = CaptureUtils::mapToCapturePixels(m_capture, selection);

        if (sourcePixels.isValid() && !sourcePixels.isEmpty()) {
            painter.save();
            painter.setClipRect(localSelection);
            painter.drawPixmap(localSelection, m_colorPixmap, sourcePixels);
            painter.restore();
        }

        // 十字辅助线让跨屏框选时更容易对准行列。
        if (m_hasCursor) {
            const QPointF cursor = QPointF(m_cursorGlobal - m_virtualRect.topLeft());
            painter.setPen(QPen(QColor(255, 255, 255, 68), 1.0, Qt::DashLine));
            painter.drawLine(QPointF(cursor.x(), 0), QPointF(cursor.x(), height()));
            painter.drawLine(QPointF(0, cursor.y()), QPointF(width(), cursor.y()));
        }
    }

    if (!m_finished) {
        paintSelectionChrome(painter, selection);
        paintInstructions(painter);
    }
}

void RegionSelector::paintSelectionChrome(QPainter &painter, const QRect &localSelection) const
{
    if (localSelection.width() <= 0 || localSelection.height() <= 0)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);

    const QRectF frame = QRectF(localSelection).adjusted(0.5, 0.5, -0.5, -0.5);

    // 浅色论文页和深色桌面都可能出现，所以边框使用“暗色投影 + 白线 +
    // 金线”的组合，保证在两类背景上都能看清选区边界。
    painter.setPen(QPen(QColor(12, 18, 27, 95), 5.0));
    painter.drawRect(frame.adjusted(-2.0, -2.0, 2.0, 2.0));
    painter.setPen(QPen(QColor(255, 255, 255, 225), 2.0));
    painter.drawRect(frame);
    painter.setPen(QPen(kSelectionGold, 1.5));
    painter.drawRect(frame.adjusted(1.75, 1.75, -1.75, -1.75));

    const qreal cornerLength = std::clamp(
        std::min(frame.width(), frame.height()) * 0.24, 8.0, 22.0);
    const qreal inset = 1.5;
    const QRectF cornerRect = frame.adjusted(inset, inset, -inset, -inset);

    auto drawCorner = [&](const QPointF &corner, qreal xDirection, qreal yDirection) {
        painter.drawLine(corner, corner + QPointF(cornerLength * xDirection, 0));
        painter.drawLine(corner, corner + QPointF(0, cornerLength * yDirection));
    };

    // 先画一条暗色加宽底线，再叠金色，使角标在奶油色页面上也有对比度。
    painter.setPen(QPen(QColor(12, 18, 27, 105), 5.0,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    drawCorner(cornerRect.topLeft(), 1.0, 1.0);
    drawCorner(cornerRect.topRight(), -1.0, 1.0);
    drawCorner(cornerRect.bottomLeft(), 1.0, -1.0);
    drawCorner(cornerRect.bottomRight(), -1.0, -1.0);

    painter.setPen(QPen(kAccent, 2.8,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    drawCorner(cornerRect.topLeft(), 1.0, 1.0);
    drawCorner(cornerRect.topRight(), -1.0, 1.0);
    drawCorner(cornerRect.bottomLeft(), 1.0, -1.0);
    drawCorner(cornerRect.bottomRight(), -1.0, -1.0);

    // 选区尺寸胶囊：优先放在选区上方，上方不够时放在选区内部。
    const QString sizeText = QStringLiteral("%1 × %2  DIP")
                                 .arg(localSelection.width())
                                 .arg(localSelection.height());
    QFont badgeFont = font();
    badgeFont.setPixelSize(12);
    badgeFont.setWeight(QFont::DemiBold);
    painter.setFont(badgeFont);

    const QFontMetricsF metrics(badgeFont);
    const qreal badgeWidth = metrics.horizontalAdvance(sizeText) + 32.0;
    const qreal badgeHeight = 30.0;
    qreal badgeX = localSelection.left();
    qreal badgeY = localSelection.top() - badgeHeight - 10.0;

    badgeX = std::clamp(badgeX, 10.0, std::max(10.0, width() - badgeWidth - 10.0));
    if (badgeY < 74.0)
        badgeY = localSelection.top() + 10.0;
    badgeY = std::clamp(badgeY, 10.0, std::max(10.0, height() - badgeHeight - 10.0));

    const QRectF badgeRect(badgeX, badgeY, badgeWidth, badgeHeight);
    painter.setPen(QPen(QColor(255, 255, 255, 28), 1.0));
    painter.setBrush(QColor(22, 29, 39, 232));
    painter.drawRoundedRect(badgeRect, 10.0, 10.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(kAccent);
    painter.drawEllipse(QPointF(badgeRect.left() + 13.0, badgeRect.center().y()), 3.0, 3.0);

    painter.setPen(QColor(0xF7, 0xF2, 0xE7));
    painter.drawText(badgeRect.adjusted(24.0, 0.0, -8.0, 0.0),
                     Qt::AlignVCenter | Qt::AlignLeft, sizeText);

    painter.restore();
}

void RegionSelector::paintInstructions(QPainter &painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QString text = m_selecting
        ? tr("松开鼠标完成选区   ·   Esc 取消")
        : tr("拖拽框选屏幕区域   ·   Esc 取消");

    QFont hintFont = font();
    hintFont.setPixelSize(12);
    hintFont.setWeight(QFont::Medium);
    painter.setFont(hintFont);

    const QFontMetricsF metrics(hintFont);
    const qreal padding = 18.0;
    const qreal dotSpace = 16.0;
    const qreal pillWidth = metrics.horizontalAdvance(text) + padding * 2.0 + dotSpace;
    const qreal pillHeight = 36.0;

    const QRect activeArea = m_instructionArea.isValid() ? m_instructionArea : rect();
    const qreal minX = activeArea.left() + 12.0;
    const qreal maxX = activeArea.right() - pillWidth - 12.0;
    const qreal centeredX = activeArea.left() + (activeArea.width() - pillWidth) / 2.0;
    const qreal pillX = maxX >= minX ? std::clamp(centeredX, minX, maxX)
                                     : activeArea.left() + 12.0;
    const qreal pillY = activeArea.top() + 22.0;
    const QRectF pill(pillX, pillY, pillWidth, pillHeight);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 38));
    painter.drawRoundedRect(pill.translated(0, 4), 13.0, 13.0);

    painter.setPen(QPen(QColor(255, 255, 255, 34), 1.0));
    painter.setBrush(QColor(19, 25, 34, 224));
    painter.drawRoundedRect(pill, 13.0, 13.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(kAccent);
    painter.drawEllipse(QPointF(pill.left() + padding, pill.center().y()), 3.5, 3.5);

    painter.setPen(QColor(0xF5, 0xF2, 0xE9));
    painter.drawText(pill.adjusted(padding + dotSpace, 0.0, -padding, 0.0),
                     Qt::AlignVCenter | Qt::AlignLeft, text);

    painter.restore();
}
