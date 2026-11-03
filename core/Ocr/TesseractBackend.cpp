#include "core/Ocr/OcrBackends.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstring>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#if defined(Q_OS_WIN) && defined(PAPERREADER_HAS_TESSERACT)

namespace
{
using TessHandle = void *;

using PfnCreate = TessHandle (*)();
using PfnDelete = void (*)(TessHandle);
using PfnInit = int (*)(TessHandle, const char *, const char *);
using PfnSetImage = void (*)(TessHandle, const unsigned char *, int, int, int, int);
using PfnSetPageSegMode = void (*)(TessHandle, int);
using PfnGetUTF8Text = char *(*)(TessHandle);
using PfnDeleteText = void (*)(char *);
using PfnGetIterator = void *(*)(TessHandle);
using PfnIteratorNext = int (*)(void *, int);
using PfnIteratorBox = int (*)(void *, int, int *, int *, int *, int *);
using PfnIteratorText = char *(*)(void *, int);
using PfnIteratorDelete = void (*)(void *);

constexpr int kRilWord = 3;          ///< TessPageIteratorLevel::RIL_WORD
constexpr int kPsmSingleWord = 8;    ///< 把图当成一个词
constexpr int kPsmBlock = 6;         ///< 当成一块均匀文本

QString runtimeDirectory()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/third_party/tesseract");
}

QString libraryPath()
{
    return runtimeDirectory() + QStringLiteral("/bin/tesseract55.dll");
}

QString tessdataPath()
{
    return runtimeDirectory() + QStringLiteral("/tessdata");
}

template <typename T>
T loadSymbol(HMODULE module, const char *name)
{
    return reinterpret_cast<T>(reinterpret_cast<void *>(::GetProcAddress(module, name)));
}

struct Engine
{
    HMODULE module = nullptr;
    TessHandle api = nullptr;

    PfnCreate create = nullptr;
    PfnDelete destroy = nullptr;
    PfnInit init = nullptr;
    PfnSetImage setImage = nullptr;
    PfnSetPageSegMode setPageSegMode = nullptr;
    PfnGetUTF8Text getText = nullptr;
    PfnDeleteText deleteText = nullptr;
    PfnGetIterator getIterator = nullptr;
    PfnIteratorNext iteratorNext = nullptr;
    PfnIteratorBox iteratorBox = nullptr;
    PfnIteratorText iteratorText = nullptr;   ///< 可选：没有导出时退回按空白分词配对
    PfnIteratorDelete iteratorDelete = nullptr;

    bool ready = false;
};

class TesseractBackend final : public Ocr::Backend
{
public:
    ~TesseractBackend() override { release(); }

    bool initialize(QString *errorMessage) override
    {
        const QString dllPath = libraryPath();
        const QString dataPath = tessdataPath();

        if (!QFileInfo::exists(dllPath) || !QFileInfo::exists(dataPath + QStringLiteral("/eng.traineddata"))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("找不到 Tesseract 运行库：%1（需要 bin/tesseract55.dll 与 tessdata/eng.traineddata）。")
                                    .arg(QDir::toNativeSeparators(runtimeDirectory()));
            }
            return false;
        }

        // Windows 只会在 exe 目录/系统目录/PATH 里找依赖 DLL，不会去被加载 DLL
        // 自己的目录找；先把 bin 加进搜索路径，否则常见结果是 ERROR_MOD_NOT_FOUND(126)。
        // 依赖在 LoadLibrary 时一次性解析完，加载后立刻恢复，避免污染进程状态。
        const QString binDir = runtimeDirectory() + QStringLiteral("/bin");
        ::SetDllDirectoryW(reinterpret_cast<LPCWSTR>(binDir.utf16()));
        m_engine.module = ::LoadLibraryW(reinterpret_cast<LPCWSTR>(dllPath.utf16()));
        const DWORD loadError = ::GetLastError();
        ::SetDllDirectoryW(nullptr);

        if (!m_engine.module) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("加载 tesseract55.dll 失败（错误码 %1，路径 %2）。")
                                    .arg(static_cast<quint32>(loadError))
                                    .arg(QDir::toNativeSeparators(dllPath));
            }
            return false;
        }

        m_engine.create = loadSymbol<PfnCreate>(m_engine.module, "TessBaseAPICreate");
        m_engine.destroy = loadSymbol<PfnDelete>(m_engine.module, "TessBaseAPIDelete");
        m_engine.init = loadSymbol<PfnInit>(m_engine.module, "TessBaseAPIInit3");
        m_engine.setImage = loadSymbol<PfnSetImage>(m_engine.module, "TessBaseAPISetImage");
        m_engine.setPageSegMode = loadSymbol<PfnSetPageSegMode>(m_engine.module, "TessBaseAPISetPageSegMode");
        m_engine.getText = loadSymbol<PfnGetUTF8Text>(m_engine.module, "TessBaseAPIGetUTF8Text");
        m_engine.deleteText = loadSymbol<PfnDeleteText>(m_engine.module, "TessDeleteText");
        m_engine.getIterator = loadSymbol<PfnGetIterator>(m_engine.module, "TessBaseAPIGetIterator");
        m_engine.iteratorNext = loadSymbol<PfnIteratorNext>(m_engine.module, "TessPageIteratorNext");
        m_engine.iteratorBox = loadSymbol<PfnIteratorBox>(m_engine.module, "TessPageIteratorBoundingBox");
        m_engine.iteratorText = loadSymbol<PfnIteratorText>(m_engine.module, "TessPageIteratorGetUTF8Text");
        m_engine.iteratorDelete = loadSymbol<PfnIteratorDelete>(m_engine.module, "TessPageIteratorDelete");

        if (!m_engine.create || !m_engine.destroy || !m_engine.init || !m_engine.setImage
            || !m_engine.setPageSegMode || !m_engine.getText || !m_engine.deleteText
            || !m_engine.getIterator || !m_engine.iteratorNext || !m_engine.iteratorBox
            || !m_engine.iteratorDelete) {
            release();
            if (errorMessage)
                *errorMessage = QStringLiteral("Tesseract DLL 缺少必要的导出函数。");
            return false;
        }

        m_engine.api = m_engine.create();
        if (!m_engine.api) {
            release();
            if (errorMessage)
                *errorMessage = QStringLiteral("Tesseract 引擎创建失败。");
            return false;
        }

        const QByteArray data = dataPath.toUtf8();
        if (m_engine.init(m_engine.api, data.constData(), "eng") != 0) {
            release();
            if (errorMessage)
                *errorMessage = QStringLiteral("Tesseract 初始化失败（tessdata 路径：%1）。")
                                    .arg(QDir::toNativeSeparators(dataPath));
            return false;
        }

        m_description = QStringLiteral("Tesseract 5 · eng");
        m_engine.ready = true;
        return true;
    }

    QString description() const override { return m_description; }

    Ocr::Result recognize(const QImage &image) override
    {
        Ocr::Result result;
        if (!m_engine.ready) {
            result.error = QStringLiteral("Tesseract 引擎尚未初始化。");
            return result;
        }

        QImage frame = image.convertToFormat(QImage::Format_RGBA8888);
        if (frame.isNull()) {
            result.error = QStringLiteral("无法把图片转换成 OCR 需要的格式。");
            return result;
        }

        // 小字号是论文截图的主要场景：先放大再识别。放大倍数要在给引擎前定好，
        // 因为词框坐标最后要按同一比例还原。
        const int sourceHeight = frame.height();
        qreal scale = 1.0;
        if (sourceHeight <= 64)
            scale = 3.0;
        else if (sourceHeight < 400)
            scale = 2.0;

        if (scale > 1.0) {
            const int width = qMin(6000, qRound(frame.width() * scale));
            if (width > frame.width()) {
                const qreal actual = static_cast<qreal>(width) / frame.width();
                const int height = qMax(1, qRound(frame.height() * actual));
                frame = frame.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                scale = actual;
            } else {
                scale = 1.0;
            }
        }

        const int pageSegMode = sourceHeight <= 64 ? kPsmSingleWord : kPsmBlock;
        m_engine.setPageSegMode(m_engine.api, pageSegMode);
        m_engine.setImage(m_engine.api, frame.constBits(), frame.width(), frame.height(), 4,
                          static_cast<int>(frame.bytesPerLine()));

        char *raw = m_engine.getText(m_engine.api);
        if (!raw) {
            result.error = QStringLiteral("Tesseract 识别失败。");
            return result;
        }
        const QString wholeText = QString::fromUtf8(raw);
        m_engine.deleteText(raw);

        void *iterator = m_engine.getIterator(m_engine.api);
        if (!iterator) {
            result.error = QStringLiteral("Tesseract 迭代器创建失败。");
            return result;
        }

        auto appendWord = [&result, scale](const QString &text, int left, int top, int right, int bottom) {
            const QString trimmed = text.trimmed();
            if (trimmed.isEmpty())
                return;
            Ocr::Word word;
            word.text = trimmed;
            word.box = QRectF(left / scale, top / scale,
                              (right - left) / scale, (bottom - top) / scale);
            result.words.append(word);
        };

        if (m_engine.iteratorText) {
            // 首选：词文本与词框来自同一个迭代器，天然对齐。
            do {
                int left = 0;
                int top = 0;
                int right = 0;
                int bottom = 0;
                if (!m_engine.iteratorBox(iterator, kRilWord, &left, &top, &right, &bottom))
                    continue;

                char *text = m_engine.iteratorText(iterator, kRilWord);
                if (!text)
                    continue;
                const QString wordText = QString::fromUtf8(text);
                m_engine.deleteText(text);
                appendWord(wordText, left, top, right, bottom);
            } while (m_engine.iteratorNext(iterator, kRilWord));
        } else {
            // 退路：DLL 没导出 TessPageIteratorGetUTF8Text，用整段文本按空白分词，
            // 再和词框按顺序配对。行内换行会退化成空格，对本功能足够。
            static const QRegularExpression whitespace(QStringLiteral("\\s+"));
            const QStringList tokens = wholeText.split(whitespace, Qt::SkipEmptyParts);
            int index = 0;
            do {
                int left = 0;
                int top = 0;
                int right = 0;
                int bottom = 0;
                if (!m_engine.iteratorBox(iterator, kRilWord, &left, &top, &right, &bottom)) {
                    ++index;
                    continue;
                }
                if (index < tokens.size())
                    appendWord(tokens.at(index), left, top, right, bottom);
                ++index;
            } while (m_engine.iteratorNext(iterator, kRilWord));
        }

        m_engine.iteratorDelete(iterator);
        return result;
    }

private:
    void release()
    {
        if (m_engine.api && m_engine.destroy)
            m_engine.destroy(m_engine.api);
        m_engine.api = nullptr;

        if (m_engine.module) {
            ::FreeLibrary(m_engine.module);
            m_engine.module = nullptr;
        }

        m_engine.ready = false;
    }

    Engine m_engine;
    QString m_description;
};
} // namespace

namespace Ocr
{
bool tesseractRuntimeAvailable()
{
    return QFileInfo::exists(libraryPath())
        && QFileInfo::exists(tessdataPath() + QStringLiteral("/eng.traineddata"));
}

std::unique_ptr<Backend> createTesseractBackend()
{
    return std::make_unique<TesseractBackend>();
}
} // namespace Ocr

#endif // Q_OS_WIN && PAPERREADER_HAS_TESSERACT
