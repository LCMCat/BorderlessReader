#include "plugins/WordLookup/lookup/LookupService.h"

#include "plugins/WordLookup/lookup/LocalDictionary.h"

#include <utility>

namespace
{
constexpr int kMaxQueryLength = 64;
} // namespace

LookupService::LookupService() = default;

LookupService::~LookupService() = default;

bool LookupService::initialize(QString *errorMessage)
{
    if (m_initialized) {
        if (errorMessage)
            *errorMessage = m_error;
        return m_ready;
    }
    m_initialized = true;

    auto local = std::make_unique<LocalDictionary>();
    QString error;
    if (!local->load(&error)) {
        m_error = error;
        if (errorMessage)
            *errorMessage = error;
        return false;
    }

    m_description = local->description();
    m_providers.push_back(std::move(local));
    m_ready = true;
    return true;
}

LookupResult LookupService::lookup(const QString &rawWord) const
{
    LookupResult result;

    if (!m_ready) {
        result.status = LookupStatus::Unavailable;
        result.message = m_error.isEmpty() ? QStringLiteral("词库尚未初始化。") : m_error;
        return result;
    }

    result.query = normalizeQuery(rawWord);
    if (result.query.isEmpty()) {
        result.status = LookupStatus::NotFound;
        result.message = QStringLiteral("没有识别到英文单词，可以直接在上方输入后按回车。");
        return result;
    }

    for (const std::unique_ptr<LookupProvider> &provider : m_providers) {
        if (!provider->isAvailable())
            continue;

        LookupResult candidate = provider->lookup(result.query);
        if (!candidate.found())
            continue;

        candidate.query = result.query;
        if (candidate.matchedFrom.isEmpty())
            candidate.matchedFrom = result.query;
        return candidate;
    }

    result.status = LookupStatus::NotFound;
    result.message = QStringLiteral("没有查到 %1。本地词库未收录该词，可以试试原形或手动输入。")
                         .arg(result.query);
    return result;
}

QString LookupService::normalizeQuery(const QString &rawWord)
{
    QString out;
    out.reserve(qMin(static_cast<int>(rawWord.size()), kMaxQueryLength));

    for (const QChar ch : rawWord) {
        if (out.size() >= kMaxQueryLength)
            break;
        if (ch.isLetter() || ch == QLatin1Char('-') || ch == QLatin1Char('\''))
            out.append(ch.toLower());
    }

    return out;
}
