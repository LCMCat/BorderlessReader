#pragma once

#include "core/IToolPlugin.h"

#include <QPointer>

class QDialog;

/**
 * @brief 框架自检用示例插件。
 *
 * 不包含任何翻译/查词业务，只在选区完成后弹出一个结果窗口：
 *  - 显示全局坐标、逻辑尺寸、截图物理像素、devicePixelRatio；
 *  - 展示实际的截图缩略图；
 *  - 用来验证 FloatBar → RegionSelector → PluginManager → IToolPlugin 全链路。
 */
class DemoPlugin final : public IToolPlugin
{
public:
    DemoPlugin() = default;
    ~DemoPlugin() override = default;

    QString name() const override;
    QIcon icon() const override;
    void onRegionSelected(const QRect &globalRect, const QPixmap &regionPixmap) override;

private:
    QPointer<QDialog> m_resultDialog;
};
