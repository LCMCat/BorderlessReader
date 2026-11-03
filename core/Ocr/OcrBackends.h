#pragma once

#include "core/Ocr/OcrBackend.h"

#include <memory>

/**
 * @brief 平台 OCR 后端的内部工厂声明。
 *
 * 只有 core/Ocr/PlatformOcrBackend.cpp 使用：它按“Tesseract → Windows OCR → 无”
 * 的优先级选一个。调用方只认 Ocr::createPlatformBackend()，不感知具体后端。
 */
namespace Ocr
{
#if defined(PAPERREADER_HAS_TESSERACT)
/// 贡献者提供的 Tesseract 运行库是否就位（只看文件，不加载引擎）。
bool tesseractRuntimeAvailable();
std::unique_ptr<Backend> createTesseractBackend();
#endif

#if defined(PAPERREADER_HAS_WINOCR)
std::unique_ptr<Backend> createWindowsBackend();
#endif
} // namespace Ocr
