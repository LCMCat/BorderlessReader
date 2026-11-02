#include "core/Utils/CaptureUtils.h"

#include "core/Utils/ScreenUtils.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QtMath>

#include <algorithm>

namespace
{
qreal captureScale()
{
    // 多屏 DPR 不同时只能选一个统一比例。取最大值可以避免高 DPI 屏幕
    // 被降采样；截图的逻辑坐标映射仍然完全一致。
    qreal scale = 1.0;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen)
            scale = std::max(scale, screen->devicePixelRatio());
    }
    return scale;
}

QRect logicalRectToPixels(const QRect &globalRect, const QPoint &origin, qreal scale)
{
    if (!globalRect.isValid() || scale <= 0.0)
        return {};

    // 全局逻辑坐标的边界是“连续”的，物理像素边界是“离散”的。把两条边
    // 分别四舍五入到最近的设备像素，比简单 floor(width * DPR) 更接近用户
    // 实际看到的选区；1.5 倍缩放下也不会固定多截取一条边缘像素。
    const qreal left = (globalRect.left() - origin.x()) * scale;
    const qreal top = (globalRect.top() - origin.y()) * scale;
    const qreal right = (globalRect.left() + globalRect.width() - origin.x()) * scale;
    const qreal bottom = (globalRect.top() + globalRect.height() - origin.y()) * scale;

    const int pixelLeft = qRound(left);
    const int pixelTop = qRound(top);
    const int pixelRight = qRound(right);
    const int pixelBottom = qRound(bottom);

    return QRect(pixelLeft,
                 pixelTop,
                 std::max(0, pixelRight - pixelLeft),
                 std::max(0, pixelBottom - pixelTop));
}

QRect mappedScreenRect(const QScreen *screen, const QPoint &captureOrigin, qreal scale)
{
    // 屏幕在虚拟桌面中的位置也要从逻辑坐标换算到统一截图像素坐标。
    return logicalRectToPixels(screen->geometry(), captureOrigin, scale);
}
} // namespace

DesktopCapture CaptureUtils::captureVirtualDesktop()
{
    DesktopCapture result;
    result.globalRect = ScreenUtils::virtualDesktopGeometry();

    if (!result.globalRect.isValid())
        return result;

    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty())
        return result;

    result.scale = captureScale();

    const QSize pixelSize(qMax(1, qRound(result.globalRect.width() * result.scale)),
                          qMax(1, qRound(result.globalRect.height() * result.scale)));

    QImage canvas(pixelSize, QImage::Format_RGB32);
    // 屏幕之间可能有坐标空隙，先铺一个中性深色，避免透明/花屏。
    canvas.fill(QColor(29, 34, 43));
    canvas.setDevicePixelRatio(1.0);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    bool capturedAnyScreen = false;

    for (QScreen *screen : screens) {
        if (!screen)
            continue;

        const QPixmap screenPixmap = screen->grabWindow(0);
        if (screenPixmap.isNull())
            continue;

        const QRect target = mappedScreenRect(screen, result.globalRect.topLeft(), result.scale);
        if (!target.isValid())
            continue;

        // drawPixmap(targetRect, pixmap) 会完整缩放到目标矩形；source 若自带
        // DPR 也不会影响最终物理像素布局。
        painter.drawPixmap(target, screenPixmap);
        capturedAnyScreen = true;
    }

    painter.end();

    if (!capturedAnyScreen)
        return {};

    result.image = canvas;
    return result;
}

QRect CaptureUtils::mapToCapturePixels(const DesktopCapture &capture, const QRect &globalRect)
{
    if (!capture.isValid() || !globalRect.isValid())
        return {};

    const QRect clipped = globalRect.intersected(capture.globalRect);
    if (!clipped.isValid())
        return {};

    return logicalRectToPixels(clipped, capture.globalRect.topLeft(), capture.scale)
        .intersected(capture.image.rect());
}

QPixmap CaptureUtils::cropRegion(const DesktopCapture &capture, const QRect &globalRect)
{
    if (!capture.isValid() || !globalRect.isValid())
        return {};

    const QRect pixelRect = mapToCapturePixels(capture, globalRect);
    if (!pixelRect.isValid() || pixelRect.isEmpty())
        return {};

    const QImage cropped = capture.image.copy(pixelRect);
    if (cropped.isNull())
        return {};

    QPixmap result = QPixmap::fromImage(cropped);
    // 让插件看到真实像素，同时让 QLabel 能用逻辑尺寸显示它。
    result.setDevicePixelRatio(capture.scale);
    return result;
}
