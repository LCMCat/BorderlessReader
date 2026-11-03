#include "core/Ocr/OcrBackends.h"

namespace Ocr
{
std::unique_ptr<Backend> createPlatformBackend()
{
#if defined(PAPERREADER_HAS_TESSERACT)
    // 优先用 Tesseract：Windows OCR 对小字号论文正文的识别率明显更差。
    if (tesseractRuntimeAvailable())
        return createTesseractBackend();
#endif

#if defined(PAPERREADER_HAS_WINOCR)
    return createWindowsBackend();
#else
    return nullptr;
#endif
}
} // namespace Ocr
