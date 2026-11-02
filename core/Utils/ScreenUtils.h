#pragma once

#include <QPoint>
#include <QRect>

class QScreen;

/**
 * @brief 屏幕几何相关的小工具。
 *
 * 所有公开坐标均为 Qt 全局逻辑坐标（DIP）。在 Windows 的 150% 缩放下，
 * 一个 2560 物理像素宽的屏幕通常对应约 1707 个 Qt 逻辑像素。不要在业务
 * 代码里手工乘除 DPR，否则多屏、不同 DPR 时很容易出现偏移。
 */
namespace ScreenUtils
{
/// 所有屏幕 geometry 的并集；可能包含不同屏幕之间的坐标空隙。
QRect virtualDesktopGeometry();

/// 查找点所在屏幕。找不到时优先返回 fallback，其次主屏，最后第一块屏幕。
QScreen *screenAtPoint(const QPoint &globalPoint, QScreen *fallback = nullptr);

/// 查找与矩形相交面积最大的屏幕。适用于窗口/悬浮条跨屏拖动后的归属判断。
QScreen *screenForRect(const QRect &globalRect, QScreen *fallback = nullptr);

/// 把全局坐标转换成某块屏幕内的局部坐标。
QPoint toScreenLocal(const QPoint &globalPoint, QScreen *screen);
QRect toScreenLocal(const QRect &globalRect, QScreen *screen);

/// 把选区限制在虚拟桌面内。
QRect clampSelectionToVirtualDesktop(const QRect &globalSelection);

/// 把窗口完整限制在屏幕可用区域（不含任务栏）内，尺寸不变。
QRect clampWindowToAvailableGeometry(const QRect &windowRect, QScreen *screen);

/// 悬浮条边缘吸附阈值，单位为逻辑像素。
int edgeSnapThreshold();

/**
 * 把窗口限制在当前屏幕可用区内，并在距边缘 threshold 像素内时吸附。
 * 多屏场景应先用 screenForRect() 找出窗口所在屏幕，再调用此函数。
 */
QRect snapToScreenEdges(const QRect &windowRect,
                        QScreen *screen,
                        int threshold = edgeSnapThreshold());
} // namespace ScreenUtils
