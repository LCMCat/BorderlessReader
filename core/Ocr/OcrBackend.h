#pragma once

#include "core/Ocr/TextRecognizer.h"

#include <QString>

#include <memory>

namespace Ocr
{
/**
 * @brief OCR 后端接口。
 *
 * 只有平台实现关心“怎么把像素变成文字”；Recognizer 负责线程、生命周期和
 * 请求编排。将来要接 Tesseract 或云端 OCR，实现这个接口、在 CMake 里换掉
 * 工厂实现文件即可，调用方不用改。
 *
 * 实现约束：
 *  - initialize() 和 recognize() 只会被同一个工作线程调用；
 *  - 不在 GUI 线程运行，禁止碰 QPixmap / QWidget；
 *  - recognize() 可以阻塞，调用方已经用工作线程兜住了。
 */
class Backend
{
public:
    virtual ~Backend() = default;

    /// 后端描述，用于状态展示。
    virtual QString description() const = 0;

    /// 初始化引擎/模型。失败时把人类可读的原因写进 errorMessage。
    virtual bool initialize(QString *errorMessage) = 0;

    /// 同步识别。失败时返回 error 非空的结果。
    virtual Result recognize(const QImage &image) = 0;
};

/// 平台后端工厂。没有可用后端时返回 nullptr（例如非 Windows 构建）。
std::unique_ptr<Backend> createPlatformBackend();
} // namespace Ocr
