#pragma once

#include <QImage>
#include <QPixmap>
#include <QRect>

/**
 * @brief 一次虚拟桌面截图的结果。
 *
 * image 是“拼好的物理像素图”，没有设置 devicePixelRatio；
 * scale 表示一个全局逻辑像素对应多少个 image 像素。
 * 这样 selector 和插件不需要猜测各块屏幕各自的 DPR。
 */
struct DesktopCapture
{
    QRect globalRect;     ///< 虚拟桌面在 Qt 全局坐标中的范围（DIP）
    QImage image;         ///< 完整虚拟桌面，尺寸为物理像素
    qreal scale = 1.0;    ///< image 像素 / 全局逻辑像素

    bool isValid() const
    {
        return !image.isNull() && globalRect.isValid() && scale > 0.0;
    }
};

/**
 * @brief 屏幕抓取和 HiDPI 裁剪工具。
 *
 * 用法：
 *   const DesktopCapture shot = CaptureUtils::captureVirtualDesktop();
 *   QPixmap selected = CaptureUtils::cropRegion(shot, globalRect);
 *
 * 返回的 QPixmap 已设置 devicePixelRatio(shot.scale)，因此 QLabel 等控件
 * 会按逻辑尺寸显示；但 pixmap.width()/height() 仍然是真实截图像素数。
 */
namespace CaptureUtils
{
/// 抓取所有屏幕并拼成整个虚拟桌面。必须在 GUI 线程调用。
DesktopCapture captureVirtualDesktop();

/**
 * 按全局选区从截图中裁剪。
 *
 * 选区超出虚拟桌面的部分会被裁掉；选区的逻辑坐标通过 DesktopCapture::scale
 * 换算成物理像素，四条边界分别取最近的设备像素，避免非整数缩放下固定多出
 * 一条边。返回的 QPixmap 的 DPR 被设置为 capture.scale。
 */
QPixmap cropRegion(const DesktopCapture &capture, const QRect &globalRect);

/**
 * 把全局逻辑矩形映射为 capture.image 中的物理像素矩形。
 * 主要用于选区器内部复用同一套 HiDPI 换算规则。
 */
QRect mapToCapturePixels(const DesktopCapture &capture, const QRect &globalRect);
} // namespace CaptureUtils
