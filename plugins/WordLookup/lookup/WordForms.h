#pragma once

#include <QHash>
#include <QString>

/**
 * @brief 词形还原表：变形 → 原形。
 *
 * 数据来自 `dict/forms_full.tsv`。这份表是双向的（既有一行 form→base，也有一行
 * base→form，还混着 exchange 编码），但查询侧总是先做精确命中，所以反向行基本
 * 不会被走到；宁可少过滤，也不要在首次加载时多花几百毫秒。
 *
 * 这里只做查表，不猜、不做规则推导。
 */
class WordForms
{
public:
    /// 加载 forms 文件。重复调用会被忽略；失败时 errorMessage 给出原因。
    bool load(const QString &path, QString *errorMessage = nullptr);

    bool isLoaded() const { return m_loaded; }
    int size() const { return m_size; }

    /// 返回原形；没有映射时返回空串。
    QString baseForm(const QString &form) const;

private:
    QHash<QString, QString> m_map;
    int m_size = 0;
    bool m_loaded = false;
};
