#pragma once

#include "core/Selection.h"

#include <QIcon>
#include <QString>

/**
 * @brief 所有框选工具的最小插件接口。
 *
 * 工具插件只关心“选区完成后做什么”，不需要知道悬浮条、遮罩或鼠标事件。
 * 需要新增查词、翻译、公式识别等功能时，实现这个接口并注册即可。
 *
 * 坐标与线程约定见 Selection：选区是逻辑坐标（DIP），截图是物理像素；
 * 需要整行/上下文文字时用 Selection::desktopImage，不要自己重新抓屏。
 *
 * onSelection() 在 GUI 线程中调用。若插件需要异步保存数据，请自行复制
 * globalRect / 图片后再使用（QImage、QPixmap 本身不保证跨线程安全）。
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
    virtual void onSelection(const Selection &selection) = 0;
};
