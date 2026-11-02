#include "core/Utils/ScreenUtils.h"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>
#include <limits>

namespace
{
QScreen *fallbackScreen(QScreen *preferred)
{
    if (preferred)
        return preferred;

    if (QScreen *primary = QGuiApplication::primaryScreen())
        return primary;

    const auto screens = QGuiApplication::screens();
    return screens.isEmpty() ? nullptr : screens.first();
}
} // namespace

QRect ScreenUtils::virtualDesktopGeometry()
{
    QRect result;

    for (QScreen *screen : QGuiApplication::screens()) {
        if (!screen)
            continue;
        result = result.united(screen->geometry());
    }

    // QGuiApplication 正常运行时至少有一块屏幕。这里仍做安全回退，
    // 避免某些测试环境或屏幕热插拔瞬间拿到无效矩形。
    if (!result.isValid()) {
        if (QScreen *fallback = fallbackScreen(nullptr))
            result = fallback->geometry();
    }

    return result;
}

QScreen *ScreenUtils::screenAtPoint(const QPoint &globalPoint, QScreen *fallback)
{
    if (QScreen *screen = QGuiApplication::screenAt(globalPoint))
        return screen;

    // screenAt() 在屏幕之间的空隙可能返回 nullptr；逐块检查 geometry，
    // 保证边界处的行为稳定，也方便没有物理多屏时进行调试。
    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen && screen->geometry().contains(globalPoint))
            return screen;
    }

    return fallbackScreen(fallback);
}

QScreen *ScreenUtils::screenForRect(const QRect &globalRect, QScreen *fallback)
{
    QScreen *bestScreen = nullptr;
    qint64 bestArea = -1;

    if (globalRect.isValid()) {
        for (QScreen *screen : QGuiApplication::screens()) {
            if (!screen)
                continue;

            const QRect intersection = screen->geometry().intersected(globalRect);
            const qint64 area = intersection.isValid()
                ? static_cast<qint64>(intersection.width()) * intersection.height()
                : 0;

            if (area > bestArea) {
                bestArea = area;
                bestScreen = screen;
            }
        }
    }

    if (bestScreen && bestArea > 0)
        return bestScreen;

    // 矩形刚好落在屏幕空隙，或面积无法判断时，退化为按中心点找屏幕。
    if (QScreen *centerScreen = QGuiApplication::screenAt(globalRect.center()))
        return centerScreen;

    return fallbackScreen(fallback);
}

QPoint ScreenUtils::toScreenLocal(const QPoint &globalPoint, QScreen *screen)
{
    if (!screen)
        return globalPoint;
    return globalPoint - screen->geometry().topLeft();
}

QRect ScreenUtils::toScreenLocal(const QRect &globalRect, QScreen *screen)
{
    if (!screen)
        return globalRect;
    return globalRect.translated(-screen->geometry().topLeft());
}

QRect ScreenUtils::clampSelectionToVirtualDesktop(const QRect &globalSelection)
{
    const QRect desktop = virtualDesktopGeometry();
    if (!globalSelection.isValid() || !desktop.isValid())
        return {};
    return globalSelection.intersected(desktop);
}

QRect ScreenUtils::clampWindowToAvailableGeometry(const QRect &windowRect, QScreen *screen)
{
    QScreen *target = fallbackScreen(screen);
    if (!target || !windowRect.isValid())
        return windowRect;

    const QRect available = target->availableGeometry();
    if (!available.isValid())
        return windowRect;

    const int maxX = std::max(available.left(), available.right() - windowRect.width() + 1);
    const int maxY = std::max(available.top(), available.bottom() - windowRect.height() + 1);
    const int x = std::clamp(windowRect.left(), available.left(), maxX);
    const int y = std::clamp(windowRect.top(), available.top(), maxY);

    return QRect(QPoint(x, y), windowRect.size());
}

int ScreenUtils::edgeSnapThreshold()
{
    // 逻辑像素，不乘 DPR；Qt 会负责把逻辑坐标映射到设备像素。
    return 28;
}

QRect ScreenUtils::snapToScreenEdges(const QRect &windowRect, QScreen *screen, int threshold)
{
    QScreen *target = fallbackScreen(screen);
    if (!target || !windowRect.isValid())
        return windowRect;

    const QRect available = target->availableGeometry();
    if (!available.isValid())
        return windowRect;

    // 先保证整个窗口在屏幕可用区内，避免拖出屏幕后无法找回。
    QRect result = clampWindowToAvailableGeometry(windowRect, target);

    const int leftDistance = std::abs(result.left() - available.left());
    const int rightDistance = std::abs(available.right() - result.right());
    const int topDistance = std::abs(result.top() - available.top());
    const int bottomDistance = std::abs(available.bottom() - result.bottom());

    const int snapDistance = std::max(0, threshold);
    int x = result.left();
    int y = result.top();

    // 左右/上下分别独立判断，因此靠近右上角时可以同时吸附两条边。
    if (leftDistance <= snapDistance)
        x = available.left();
    else if (rightDistance <= snapDistance)
        x = available.right() - result.width() + 1;

    if (topDistance <= snapDistance)
        y = available.top();
    else if (bottomDistance <= snapDistance)
        y = available.bottom() - result.height() + 1;

    return QRect(QPoint(x, y), result.size());
}
