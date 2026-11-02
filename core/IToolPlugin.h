#pragma once

#include <QIcon>
#include <QPixmap>
#include <QRect>
#include <QString>

/**
 * @brief 所有框选工具的最小插件接口。
 *
 * 工具插件只关心“选区完成后做什么”，不需要知道悬浮条、遮罩或鼠标事件。
 * 需要新增查词、翻译、公式识别等功能时，实现这个接口并注册即可。
 *
 * 坐标约定：
 *  - globalRect 使用 Qt 全局逻辑像素（device-independent pixels，DIP）；
 *  - regionPixmap 是实际屏幕像素裁剪结果，在 HiDPI 屏幕上通常大于
 *    globalRect 的宽高，并带有 devicePixelRatio()。
 *
 * onRegionSelected() 在 GUI 线程中调用。若插件需要异步保存数据，请自行复制
 * globalRect 和 regionPixmap 后再使用。
 */
class IToolPlugin
{
public:
    virtual ~IToolPlugin() = default;

    /// 悬浮条按钮上展示的名称。
    virtual QString name() const = 0;

    /// 悬浮条按钮上展示的图标。
    virtual QIcon icon() const = 0;

    /// 用户在屏幕上完成框选后回调。
    virtual void onRegionSelected(const QRect &globalRect,
                                  const QPixmap &regionPixmap) = 0;
};
