#include "core/Ocr/OcrBackend.h"
#include "core/Ocr/OcrBackends.h"

#include <QImage>
#include <QString>

#include <cstring>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
// C++/WinRT 在 C++17 模式下会 include <experimental/coroutine>；新版 MSVC 把这个头
// 标成“即将移除”并直接报错。我们只用同步接口，先把这条警告静音。
#  if defined(_MSC_VER) && !defined(_SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS)
#    define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS
#  endif
#endif

#if defined(Q_OS_WIN) && defined(PAPERREADER_HAS_WINOCR)
#  include <winrt/base.h>
#  include <winrt/Windows.Foundation.h>
#  include <winrt/Windows.Foundation.Collections.h>
#  include <winrt/Windows.Globalization.h>
#  include <winrt/Windows.Graphics.Imaging.h>
#  include <winrt/Windows.Media.Ocr.h>
#  include <winrt/Windows.Security.Cryptography.h>
#  include <winrt/Windows.Storage.Streams.h>
#endif

namespace
{
#if defined(Q_OS_WIN) && defined(PAPERREADER_HAS_WINOCR)

using winrt::Windows::Globalization::Language;
using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::Media::Ocr::OcrEngine;

QString fromHString(const winrt::hstring &value)
{
    return QString::fromWCharArray(value.c_str(), static_cast<int>(value.size()));
}

/**
 * 转成 Windows OCR 能吃的 BGRA8。
 *
 * Windows.Media.Ocr 只接受 BitmapPixelFormat::Bgra8，而 Qt 的 Format_ARGB32
 * 在小端内存里正好是 B,G,R,A 四个字节，可以直接交给 SoftwareBitmap。
 * 32 位格式的扫描线天然 4 字节对齐，bytesPerLine 一定等于 width * 4；
 * 这里再确认一次，避免把带 padding 的图送进去。
 */
QImage toBgra8(const QImage &source)
{
    QImage frame = source.convertToFormat(QImage::Format_ARGB32);
    if (frame.isNull())
        return {};

    if (frame.bytesPerLine() == frame.width() * 4)
        return frame;

    QImage tight(frame.width(), frame.height(), QImage::Format_ARGB32);
    if (tight.isNull())
        return {};
    for (int y = 0; y < frame.height(); ++y) {
        std::memcpy(tight.scanLine(y), frame.constScanLine(y),
                    static_cast<std::size_t>(frame.width()) * 4);
    }
    return tight;
}

class WindowsOcrBackend final : public Ocr::Backend
{
public:
    QString description() const override { return m_description; }

    bool initialize(QString *errorMessage) override
    {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (const winrt::hresult_error &) {
            // 线程已经初始化过 COM（模式不同）。继续用当前 apartment 即可。
        } catch (...) {
        }

        try {
            // 论文是英文，优先要英文引擎；没有英文语言包时退回用户配置的语言。
            if (OcrEngine::IsLanguageSupported(Language{ L"en-US" }))
                m_engine = OcrEngine::TryCreateFromLanguage(Language{ L"en-US" });
            if (!m_engine)
                m_engine = OcrEngine::TryCreateFromUserProfileLanguages();

            if (!m_engine) {
                if (errorMessage) {
                    *errorMessage = QStringLiteral(
                        "系统没有可用的 OCR 语言包。请在 Windows 设置中安装“英语”"
                        "语言包后重试。");
                }
                return false;
            }

            m_description = QStringLiteral("Windows.Media.Ocr · %1")
                                .arg(fromHString(m_engine.RecognizerLanguage().LanguageTag()));
            return true;
        } catch (const winrt::hresult_error &error) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Windows OCR 初始化失败（0x%1）。")
                                    .arg(static_cast<quint32>(error.code()), 8, 16, QLatin1Char('0'));
            }
            return false;
        } catch (...) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Windows OCR 初始化失败（未知错误）。");
            return false;
        }
    }

    Ocr::Result recognize(const QImage &image) override
    {
        Ocr::Result result;
        if (!m_engine) {
            result.error = QStringLiteral("OCR 引擎尚未初始化。");
            return result;
        }

        const QImage frame = toBgra8(image);
        if (frame.isNull()) {
            result.error = QStringLiteral("无法把图片转换成 OCR 需要的格式。");
            return result;
        }

        try {
            // 超过引擎上限的图先等比缩小；识别完把坐标按同一比例放回去，
            // 保证调用方拿到的永远是“原图像素坐标”。
            QImage input = frame;
            qreal restore = 1.0;
            const auto maxSide = static_cast<qreal>(m_engine.MaxImageDimension());
            if (maxSide > 0.0
                && (input.width() > maxSide || input.height() > maxSide)) {
                const qreal factor = qMin(maxSide / input.width(), maxSide / input.height());
                const QSize target(qMax(1, qRound(input.width() * factor)),
                                   qMax(1, qRound(input.height() * factor)));
                input = input.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                restore = static_cast<qreal>(frame.width()) / input.width();
            }

            const auto byteCount = static_cast<uint32_t>(input.sizeInBytes());
            const auto *bytes = input.constBits();
            const auto buffer =
                winrt::Windows::Security::Cryptography::CryptographicBuffer::CreateFromByteArray(
                    winrt::array_view<uint8_t const>(bytes, bytes + byteCount));

            const SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
                buffer, BitmapPixelFormat::Bgra8, input.width(), input.height(),
                BitmapAlphaMode::Premultiplied);

            const auto recognition = m_engine.RecognizeAsync(bitmap).get();
            for (const auto &line : recognition.Lines()) {
                for (const auto &word : line.Words()) {
                    const QString text = fromHString(word.Text()).trimmed();
                    if (text.isEmpty())
                        continue;

                    const auto rect = word.BoundingRect();
                    Ocr::Word item;
                    item.text = text;
                    item.box = QRectF(rect.X * restore, rect.Y * restore,
                                      rect.Width * restore, rect.Height * restore);
                    result.words.append(item);
                }
            }
        } catch (const winrt::hresult_error &error) {
            result.words.clear();
            result.error = QStringLiteral("OCR 识别失败（0x%1）。")
                               .arg(static_cast<quint32>(error.code()), 8, 16, QLatin1Char('0'));
        } catch (...) {
            result.words.clear();
            result.error = QStringLiteral("OCR 识别失败（未知错误）。");
        }

        return result;
    }

private:
    OcrEngine m_engine{ nullptr };
    QString m_description;
};

#endif // Q_OS_WIN && PAPERREADER_HAS_WINOCR
} // namespace

namespace Ocr
{
std::unique_ptr<Backend> createWindowsBackend()
{
    return std::make_unique<WindowsOcrBackend>();
}
} // namespace Ocr
