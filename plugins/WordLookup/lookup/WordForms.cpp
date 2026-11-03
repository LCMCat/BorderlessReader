#include "plugins/WordLookup/lookup/WordForms.h"

#include <QFile>
#include <QStringConverter>
#include <QTextStream>

#include <utility>

bool WordForms::load(const QString &path, QString *errorMessage)
{
    if (m_loaded)
        return true;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("打不开词形表：%1").arg(path);
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);

    QHash<QString, QString> map;
    map.reserve(120000);

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const int tab = line.indexOf(QLatin1Char('\t'));
        if (tab <= 0)
            continue;

        const QString form = line.left(tab).trimmed().toLower();
        const QString base = line.mid(tab + 1).trimmed().toLower();
        if (form.isEmpty() || base.isEmpty() || form == base)
            continue;

        map.insert(form, base);
    }

    if (map.isEmpty()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("词形表是空的：%1").arg(path);
        return false;
    }

    m_map = std::move(map);
    m_size = static_cast<int>(m_map.size());
    m_loaded = true;
    return true;
}

QString WordForms::baseForm(const QString &form) const
{
    if (!m_loaded)
        return {};
    return m_map.value(form.trimmed().toLower());
}
