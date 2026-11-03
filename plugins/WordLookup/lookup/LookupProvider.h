#pragma once

#include "plugins/WordLookup/lookup/LookupResult.h"

#include <QString>

/**
 * @brief 查词数据源接口。
 *
 * 现在只有本地 ECDICT；以后要加在线词典，只需要实现这个接口并插到
 * LookupService 的 provider 列表里，插件和 UI 都不用改。
 *
 * 约定：
 *  - 未收录返回 LookupStatus::NotFound，而不是空 WordEntry；
 *  - 数据源自己负责它擅长的词形处理（例如本地词库可以按变形表还原），
 *    并把 MatchKind / matchedFrom 填好；
 *  - 不允许联网的实现把自己标记为 isAvailable() == false 时仍返回结果。
 */
class LookupProvider
{
public:
    virtual ~LookupProvider() = default;

    /// 数据源名称，用于状态展示。
    virtual QString name() const = 0;

    /// 数据是否可用（加载成功）。
    virtual bool isAvailable() const = 0;

    /// 查一个词。同步调用，调用方在 GUI 线程，必须足够快。
    virtual LookupResult lookup(const QString &word) = 0;
};
