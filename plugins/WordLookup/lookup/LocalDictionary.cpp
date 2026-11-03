#include "plugins/WordLookup/lookup/LocalDictionary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>

#include <cstring>
#include <utility>

namespace
{
constexpr int kSparseStep = 64;
constexpr int kMaxSensesPerGroup = 6;
constexpr int kMaxGroups = 8;
constexpr int kMaxInflections = 8;

/// ECDICT 的词性缩写 → 中文标签。查不到就返回空串，宁可保留原文也不瞎归类。
QString normalizePartOfSpeech(const QString &abbr)
{
    static const QHash<QString, QString> table = {
        { QStringLiteral("n"), QStringLiteral("名词") },
        { QStringLiteral("v"), QStringLiteral("动词") },
        { QStringLiteral("vt"), QStringLiteral("动词") },
        { QStringLiteral("vi"), QStringLiteral("动词") },
        { QStringLiteral("a"), QStringLiteral("形容词") },
        { QStringLiteral("adj"), QStringLiteral("形容词") },
        { QStringLiteral("ad"), QStringLiteral("副词") },
        { QStringLiteral("adv"), QStringLiteral("副词") },
        { QStringLiteral("prep"), QStringLiteral("介词") },
        { QStringLiteral("conj"), QStringLiteral("连词") },
        { QStringLiteral("pron"), QStringLiteral("代词") },
        { QStringLiteral("num"), QStringLiteral("数词") },
        { QStringLiteral("art"), QStringLiteral("冠词") },
        { QStringLiteral("aux"), QStringLiteral("助动词") },
        { QStringLiteral("int"), QStringLiteral("感叹词") },
        { QStringLiteral("abbr"), QStringLiteral("缩写") },
        { QStringLiteral("pl"), QStringLiteral("复数") },
        { QStringLiteral("pref"), QStringLiteral("前缀") },
        { QStringLiteral("suf"), QStringLiteral("后缀") },
    };
    return table.value(abbr);
}

QStringList splitMeanings(const QString &body)
{
    static const QRegularExpression separator(QStringLiteral("[,，;；]"));
    QStringList out;
    const QStringList pieces = body.split(separator, Qt::SkipEmptyParts);
    for (const QString &piece : pieces) {
        const QString trimmed = piece.trimmed();
        if (!trimmed.isEmpty())
            out.append(trimmed);
    }
    return out;
}

/// exchange 字段的编码：p 过去式 / d 过去分词 / i 现在分词 / 3 三单 / s 复数 / r 比较级 / t 最高级。
QString inflectionLabel(const QString &code)
{
    static const QHash<QString, QString> table = {
        { QStringLiteral("p"), QStringLiteral("过去式") },
        { QStringLiteral("d"), QStringLiteral("过去分词") },
        { QStringLiteral("i"), QStringLiteral("现在分词") },
        { QStringLiteral("3"), QStringLiteral("第三人称单数") },
        { QStringLiteral("r"), QStringLiteral("比较级") },
        { QStringLiteral("t"), QStringLiteral("最高级") },
        { QStringLiteral("s"), QStringLiteral("复数") },
    };
    return table.value(code);
}

void parseTranslation(const QString &raw, QVector<WordSense> &senses)
{
    // 数据里两种换行都有：字面量 "\n" / "\r\n"，以及真实换行。全部当分隔符。
    static const QRegularExpression lineBreak(QStringLiteral(R"(\\r\\n|\\n|\r\n|\n)"));
    const QStringList lines = raw.split(lineBreak, Qt::SkipEmptyParts);

    for (const QString &rawLine : lines) {
        QString body = rawLine.trimmed();
        if (body.isEmpty())
            continue;

        QString label = QStringLiteral("释义");

        if (body.startsWith(QLatin1Char('['))) {
            // 领域标注：[计] [化] [医] ...
            const int close = body.indexOf(QLatin1Char(']'));
            if (close > 1 && close <= 4) {
                label = body.mid(1, close - 1);
                body = body.mid(close + 1).trimmed();
            }
        } else {
            // 词性缩写：n. / vt. / adj. ...
            const int dot = body.indexOf(QLatin1Char('.'));
            if (dot > 0 && dot <= 4) {
                const QString abbr = body.left(dot).trimmed().toLower();
                const QString pos = normalizePartOfSpeech(abbr);
                if (!pos.isEmpty()) {
                    label = pos;
                    body = body.mid(dot + 1).trimmed();
                }
            }
        }

        const QStringList meanings = splitMeanings(body);
        if (meanings.isEmpty())
            continue;

        int target = -1;
        for (int i = 0; i < senses.size(); ++i) {
            if (senses.at(i).partOfSpeech == label) {
                target = i;
                break;
            }
        }
        if (target < 0) {
            if (senses.size() >= kMaxGroups)
                continue;
            senses.append(WordSense{});
            target = senses.size() - 1;
            senses[target].partOfSpeech = label;
        }

        for (const QString &meaning : meanings) {
            if (senses[target].definitions.size() >= kMaxSensesPerGroup)
                break;
            senses[target].definitions.append(meaning);
            senses[target].examples.append(QString());
        }
    }
}

void parseExchange(const QString &raw, QStringList &inflections)
{
    if (raw.isEmpty())
        return;

    QHash<QString, QStringList> byCode;
    const QStringList parts = raw.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const int colon = part.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;

        const QString code = part.left(colon).trimmed();
        if (inflectionLabel(code).isEmpty())
            continue;   // 0 / 1 是“原型”标记，对读者没意义

        const QStringList forms = part.mid(colon + 1).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &form : forms) {
            const QString trimmed = form.trimmed();
            if (!trimmed.isEmpty())
                byCode[code].append(trimmed);
        }
    }

    // 同一个词形常被标两种（papers 既是复数又是三单），按对读者最有用的顺序去重展示。
    static const QStringList preferred = {
        QStringLiteral("s"), QStringLiteral("p"), QStringLiteral("i"),
        QStringLiteral("d"), QStringLiteral("r"), QStringLiteral("t"),
        QStringLiteral("3"),
    };

    QSet<QString> seen;
    for (const QString &code : preferred) {
        const QStringList forms = byCode.value(code);
        for (const QString &form : forms) {
            if (seen.contains(form))
                continue;
            seen.insert(form);
            if (inflections.size() >= kMaxInflections)
                return;
            inflections.append(inflectionLabel(code) + QLatin1Char(' ') + form);
        }
    }
}

void parseLine(const char *line, int length, WordEntry &entry)
{
    QString fields[4];
    int fieldIndex = 0;
    int start = 0;

    for (int i = 0; i <= length && fieldIndex < 4; ++i) {
        if (i != length && line[i] != '\t')
            continue;

        QString field = QString::fromUtf8(line + start, i - start);
        field.remove(QLatin1Char('\r'));
        fields[fieldIndex] = std::move(field);
        ++fieldIndex;
        start = i + 1;
    }

    if (fields[0].isEmpty())
        return;

    entry.word = fields[0];

    const QString phonetic = fields[1].trimmed();
    if (!phonetic.isEmpty()) {
        entry.phonetic = phonetic.startsWith(QLatin1Char('/'))
                             ? phonetic
                             : QLatin1Char('/') + phonetic + QLatin1Char('/');
    }

    parseTranslation(fields[2], entry.senses);
    parseExchange(fields[3], entry.inflections);
}
} // namespace

QString LocalDictionary::defaultDataDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/dict");
}

LocalDictionary::LocalDictionary(QString dataDir)
    : m_dataDir(std::move(dataDir))
{
}

LocalDictionary::~LocalDictionary() = default;

bool LocalDictionary::load(QString *errorMessage)
{
    if (m_loaded)
        return true;
    if (!m_error.isEmpty()) {
        if (errorMessage)
            *errorMessage = m_error;
        return false;
    }

    const QDir dir(m_dataDir);
    const QString entriesPath = dir.filePath(QStringLiteral("ecdict_full.tsv"));
    const QString formsPath = dir.filePath(QStringLiteral("forms_full.tsv"));

    if (!QFileInfo::exists(entriesPath)) {
        m_error = QStringLiteral("找不到词库文件：%1。请把 dict/ 放在程序同目录。")
                      .arg(QDir::toNativeSeparators(entriesPath));
        if (errorMessage)
            *errorMessage = m_error;
        return false;
    }

    m_file.setFileName(entriesPath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("打不开词库文件：%1").arg(QDir::toNativeSeparators(entriesPath));
        if (errorMessage)
            *errorMessage = m_error;
        return false;
    }

    m_size = m_file.size();
    if (m_size <= 0) {
        m_error = QStringLiteral("词库文件是空的：%1").arg(QDir::toNativeSeparators(entriesPath));
        if (errorMessage)
            *errorMessage = m_error;
        return false;
    }

    m_data = reinterpret_cast<const char *>(m_file.map(0, m_size));
    if (!m_data) {
        m_error = QStringLiteral("无法映射词库文件：%1").arg(QDir::toNativeSeparators(entriesPath));
        if (errorMessage)
            *errorMessage = m_error;
        return false;
    }

    if (!indexEntries(errorMessage))
        return false;

    // 说明：forms_full.tsv 是双向表（form→base 与 base→form 混在一起，还混着
    // exchange 编码）。查询侧总是先做精确命中，所以 base→form 的反向行基本不会
    // 被走到；这里不为过滤多花几百毫秒。
    if (!m_forms.load(formsPath, errorMessage)) {
        m_error = errorMessage ? *errorMessage : QStringLiteral("词形表加载失败。");
        return false;
    }

    m_loaded = true;
    m_error.clear();
    return true;
}

bool LocalDictionary::indexEntries(QString *errorMessage)
{
    quint32 lineStart = 0;
    int lineIndex = 0;
    QString previousWord;

    for (qint64 i = 0; i <= m_size; ++i) {
        const bool atEnd = (i == m_size);
        if (!atEnd && m_data[i] != '\n')
            continue;

        if (i > static_cast<qint64>(lineStart)) {
            if (lineIndex % kSparseStep == 0) {
                const char *line = m_data + lineStart;
                const int length = static_cast<int>(i - lineStart);

                int tab = 0;
                while (tab < length && line[tab] != '\t')
                    ++tab;

                const QString word = QString::fromUtf8(line, tab);
                if (!previousWord.isEmpty() && word < previousWord) {
                    m_error = QStringLiteral("词库未按词头排序（%1 之后出现 %2）。")
                                  .arg(previousWord, word);
                    if (errorMessage)
                        *errorMessage = m_error;
                    return false;
                }
                previousWord = word;

                m_sparseOffsets.append(lineStart);
                m_sparseWords.append(word);
            }
            ++lineIndex;
        }

        lineStart = static_cast<quint32>(i + 1);
    }

    m_entryCount = lineIndex;
    if (m_entryCount <= 0) {
        m_error = QStringLiteral("词库文件里没有词条。");
        if (errorMessage)
            *errorMessage = m_error;
        return false;
    }
    return true;
}

quint32 LocalDictionary::lineEndAt(quint32 offset) const
{
    const char *begin = m_data + offset;
    const void *found = std::memchr(begin, '\n', static_cast<std::size_t>(m_size - offset));
    if (!found)
        return static_cast<quint32>(m_size);
    return static_cast<quint32>(static_cast<const char *>(found) - m_data);
}

bool LocalDictionary::findEntry(const QString &word, WordEntry &entry) const
{
    if (m_sparseWords.isEmpty() || !m_data)
        return false;

    // 二分到最后一个“首词 <= word”的稀疏块，再顺序扫完这一块。
    int low = 0;
    int high = m_sparseWords.size() - 1;
    int best = 0;
    while (low <= high) {
        const int mid = (low + high) / 2;
        if (m_sparseWords.at(mid) <= word) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    quint32 position = m_sparseOffsets.at(best);
    const quint32 stop = (best + 1 < m_sparseOffsets.size())
                             ? m_sparseOffsets.at(best + 1)
                             : static_cast<quint32>(m_size);

    while (position < stop) {
        const quint32 end = lineEndAt(position);
        const int length = static_cast<int>(end - position);
        const char *line = m_data + position;

        int tab = 0;
        while (tab < length && line[tab] != '\t')
            ++tab;
        const QString current = QString::fromUtf8(line, tab);

        if (current == word) {
            parseLine(line, length, entry);
            return true;
        }
        if (current > word)
            return false;   // 文件有序，超过目标就不必再扫

        position = end + 1;
    }

    return false;
}

QString LocalDictionary::name() const
{
    return QStringLiteral("ECDICT（本地）");
}

QString LocalDictionary::description() const
{
    return QStringLiteral("ECDICT 全量 · %1 词条").arg(m_entryCount);
}

LookupResult LocalDictionary::lookup(const QString &word)
{
    LookupResult result;
    result.provider = name();

    if (!m_loaded) {
        result.status = LookupStatus::Unavailable;
        result.message = m_error.isEmpty() ? QStringLiteral("词库尚未加载。") : m_error;
        return result;
    }

    const QString key = word.trimmed().toLower();
    result.query = key;
    if (key.isEmpty()) {
        result.message = QStringLiteral("没有可查询的单词。");
        return result;
    }

    WordEntry entry;
    if (findEntry(key, entry)) {
        result.status = LookupStatus::Found;
        result.kind = MatchKind::Exact;
        result.matchedFrom = key;
        result.entry = std::move(entry);
        return result;
    }

    // 词形还原只查数据表：papers → paper、studies → study。
    const QString base = m_forms.baseForm(key);
    if (!base.isEmpty() && base != key && findEntry(base, entry)) {
        result.status = LookupStatus::Found;
        result.kind = MatchKind::Lemma;
        result.matchedFrom = key;   // 展示“由 prayerbooks 还原”，entry.word 是原形
        if (entry.word.isEmpty())
            entry.word = base;
        result.entry = std::move(entry);
        return result;
    }

    result.status = LookupStatus::NotFound;
    result.message = QStringLiteral("没有查到 %1。本地词库未收录该词。").arg(key);
    return result;
}
