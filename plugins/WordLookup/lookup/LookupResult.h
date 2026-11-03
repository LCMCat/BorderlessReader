#pragma once

#include "plugins/WordLookup/lookup/WordEntry.h"

/// 一次查询的结局。未收录是正常结局，不是错误。
enum class LookupStatus
{
    Found,
    NotFound,
    Unavailable,   ///< 词库缺失/加载失败，属于能力问题
};

/// 命中方式。Fuzzy 预留给以后可选的拼写纠正，默认不启用。
enum class MatchKind
{
    Exact,
    Lemma,
    Fuzzy,
};

/**
 * @brief 查词结果。
 *
 * 来源与命中方式是一等字段：UI 不需要再靠字符串比较去猜“这是不是猜的”。
 */
struct LookupResult
{
    LookupStatus status = LookupStatus::NotFound;
    MatchKind kind = MatchKind::Exact;

    WordEntry entry;
    QString query;        ///< 用户/OCR 给的原始词（已归一化）
    QString matchedFrom;  ///< 实际命中的词形；与 query 不同表示做过词形还原
    QString provider;     ///< 命中的数据源名称
    QString message;      ///< NotFound / Unavailable 时给用户看的原因

    bool found() const { return status == LookupStatus::Found; }
    bool unavailable() const { return status == LookupStatus::Unavailable; }
};
