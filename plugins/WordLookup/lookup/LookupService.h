#pragma once

#include "plugins/WordLookup/lookup/LookupProvider.h"

#include <QString>

#include <memory>
#include <vector>

/**
 * @brief 查词编排：按顺序询问 provider，返回第一个命中。
 *
 * 策略在这里，不在 UI 里：
 *  - 未收录 / 词库不可用是两种不同结局，分别上报；
 *  - 现在只有本地 ECDICT；在线 provider 以后直接插进 m_providers 即可；
 *  - 不做模糊纠错。MatchKind::Fuzzy 只是预留，默认没有实现。
 *
 * 同步调用（本地词库是 mmap 查表，微秒级），调用方在 GUI 线程。
 */
class LookupService
{
public:
    LookupService();
    ~LookupService();

    /// 加载默认本地词库（<exe>/dict）。只尝试一次；失败后 error() 给出原因。
    bool initialize(QString *errorMessage = nullptr);

    bool isReady() const { return m_ready; }
    QString description() const { return m_description; }
    QString error() const { return m_error; }

    /// 查一个词。未收录返回 NotFound，词库没加载成功返回 Unavailable。
    LookupResult lookup(const QString &rawWord) const;

    /// 归一化查询词：只保留字母、连字符、撇号，转小写。
    static QString normalizeQuery(const QString &rawWord);

private:
    std::vector<std::unique_ptr<LookupProvider>> m_providers;
    QString m_description;
    QString m_error;
    bool m_ready = false;
    bool m_initialized = false;
};
