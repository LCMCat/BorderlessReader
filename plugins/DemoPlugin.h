#pragma once

#include "core/IToolPlugin.h"
#include "core/Ocr/TextRecognizer.h"

#include <QPointer>

class QDialog;

/**
 * @brief 框架自检用示例插件。
 *
 * 不包含任何翻译/查词业务，只在选区完成后弹出一个结果窗口：
 *  - 显示全局坐标、逻辑尺寸、截图物理像素、devicePixelRatio；
 *  - 展示实际的截图缩略图；
 *  - 调用 core/Ocr 把选区里的文字和词框显示出来；
 *  - 用来验证 FloatBar → RegionSelector → PluginManager → IToolPlugin → core/Ocr 全链路。
 */
class DemoPlugin final : public IToolPlugin
{
public:
    DemoPlugin() = default;
    ~DemoPlugin() override = default;

    QString name() const override;
    QIcon icon() const override;
    void onSelection(const Selection &selection) override;

private:
    Ocr::Recognizer m_recognizer;
    Ocr::Recognizer::RequestId m_ocrRequest = 0;
    QPointer<QDialog> m_resultDialog;
};
