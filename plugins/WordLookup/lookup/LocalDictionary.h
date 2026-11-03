#pragma once

#include "plugins/WordLookup/lookup/LookupProvider.h"
#include "plugins/WordLookup/lookup/WordForms.h"

#include <QFile>
#include <QString>
#include <QVector>

/**
 * @brief 本地 ECDICT 词库（mmap + 稀疏索引）。
 *
 * 数据目录固定为 exe 同级的 `dict/`，里面必须同时有：
 *   - ecdict_full.tsv  word \t phonetic \t translation \t exchange（按 word 排序）
 *   - forms_full.tsv   变形 \t 原形
 *
 * 实现要点：
 *  - 词条正文 mmap 按需读，不整表进堆；常驻的只有每 64 行一条的稀疏索引；
 *  - 未收录时返回 NotFound；数据没加载成功返回 Unavailable，两者不混用；
 *  - 词形还原只查数据表，不猜、不做规则推导。
 */
class LocalDictionary final : public LookupProvider
{
public:
    /// 固定数据目录：<exe>/dict。
    static QString defaultDataDir();

    explicit LocalDictionary(QString dataDir = defaultDataDir());
    ~LocalDictionary() override;

    /// 加载词库与词形表。失败时 errorMessage 给出可读原因。
    bool load(QString *errorMessage = nullptr);

    bool isAvailable() const override { return m_loaded; }
    QString name() const override;
    LookupResult lookup(const QString &word) override;

    QString description() const;
    int entryCount() const { return m_entryCount; }
    QString loadError() const { return m_error; }

private:
    bool indexEntries(QString *errorMessage);

    /// 在排序文件里定位词条，命中时填充 entry。
    bool findEntry(const QString &word, WordEntry &entry) const;

    quint32 lineEndAt(quint32 offset) const;

    QString m_dataDir;
    QFile m_file;
    const char *m_data = nullptr;
    qint64 m_size = 0;
    QVector<quint32> m_sparseOffsets;
    QVector<QString> m_sparseWords;
    WordForms m_forms;
    int m_entryCount = 0;
    QString m_error;
    bool m_loaded = false;
};
