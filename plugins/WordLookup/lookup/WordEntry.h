#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * @brief 一个词性（或领域标注）下的一组义项。
 *
 * 顺序沿用数据源顺序；UI 只负责展示，不重新排序。
 */
struct WordSense
{
    QString partOfSpeech;
    QStringList definitions;
    QStringList examples;   ///< 与 definitions 一一对应，没有例句时为空串
};

/// 词典里的一个词条。
struct WordEntry
{
    QString word;
    QString phonetic;
    QVector<WordSense> senses;
    QStringList inflections;

    /// 词条是否存在。注意：存在但没有可用释义时也算存在，由 UI 决定怎么提示。
    bool isValid() const { return !word.isEmpty(); }

    /// 拼成纯文本，供“复制释义”使用。
    QString toPlainText() const
    {
        QString text = word;
        if (!phonetic.isEmpty())
            text += QStringLiteral("  ") + phonetic;
        text += QLatin1Char('\n');

        for (const WordSense &sense : senses) {
            text += QLatin1Char('\n') + sense.partOfSpeech + QLatin1Char('\n');
            for (int i = 0; i < sense.definitions.size(); ++i) {
                text += QStringLiteral("%1. %2\n").arg(i + 1).arg(sense.definitions.at(i));
                const QString example = sense.examples.value(i);
                if (!example.isEmpty())
                    text += QStringLiteral("   例：%1\n").arg(example);
            }
        }
        return text;
    }
};
