#pragma once

#include <QImage>
#include <QPixmap>
#include <QRect>

/**
 * @brief 一次框选的结果。
 *
 * 坐标约定与所有权：
 *  - globalRect 使用 Qt 全局逻辑坐标（DIP）；
 *  - regionPixmap 是选区裁剪，物理像素，devicePixelRatio() = desktopScale；
 *  - desktopImage 是整屏快照（物理像素，devicePixelRatio 恒为 1），
 *    desktopRect 是它覆盖的全局逻辑矩形。整屏快照是隐式共享的，拷贝代价很低；
 *    插件需要上下文（例如整行 OCR）时用它，**不要自己重新抓屏**；
 *  - desktopImage 允许为空（例如测试或非框选来源），此时只有 regionPixmap 可用。
 */
struct Selection
{
    QRect globalRect;
    QPixmap regionPixmap;

    QImage desktopImage;
    QRect desktopRect;
    qreal desktopScale = 1.0;
};
