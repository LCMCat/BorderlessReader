#include "plugins/WordLookup/WordPicker.h"

#include <QPointF>

namespace
{
QString tidyWord(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (const QChar ch : raw) {
        if (ch.isLetter() || ch == QLatin1Char('-') || ch == QLatin1Char('\''))
            out.append(ch);
    }
    return out;
}

bool hasTwoLetters(const QString &text)
{
    int count = 0;
    for (const QChar ch : text) {
        if (ch.isLetter() && ++count >= 2)
            return true;
    }
    return false;
}

qreal distanceSquared(const QPointF &a, const QPointF &b)
{
    const QPointF delta = a - b;
    return delta.x() * delta.x() + delta.y() * delta.y();
}
} // namespace

WordPicker::PickedWord WordPicker::pick(const QVector<Ocr::Word> &words, const QRectF &selection)
{
    PickedWord picked;
    if (words.isEmpty())
        return picked;

    struct Candidate
    {
        QString text;
        QRectF box;
    };

    QVector<Candidate> candidates;
    candidates.reserve(words.size());
    for (const Ocr::Word &word : words) {
        const QString text = tidyWord(word.text);
        if (!hasTwoLetters(text))
            continue;   // 表格线、单个标点会被识别成碎字，不参与挑选
        candidates.append(Candidate{ text, word.box });
    }
    if (candidates.isEmpty())
        return picked;

    if (selection.isValid()) {
        QVector<Candidate> overlapping;
        for (const Candidate &candidate : candidates) {
            if (candidate.box.intersects(selection))
                overlapping.append(candidate);
        }

        if (overlapping.size() == 1) {
            picked.text = overlapping.first().text;
            picked.box = overlapping.first().box;
            picked.certain = true;
            return picked;
        }

        if (!overlapping.isEmpty()) {
            // 选区里落进多个词：选“选区中心落在其词框内”的那个。
            // 用户的框选中心通常就是目标词，这样即使框大了一点、跨到上下相邻行，
            // 也不会被行首/行尾更长的词抢走。
            const QPointF center = selection.center();

            QVector<Candidate> centering;
            for (const Candidate &candidate : overlapping) {
                if (candidate.box.contains(center))
                    centering.append(candidate);
            }

            const QVector<Candidate> &pool = centering.isEmpty() ? overlapping : centering;
            const Candidate *best = nullptr;
            qreal bestDistance = 0.0;

            for (const Candidate &candidate : pool) {
                const qreal distance = distanceSquared(candidate.box.center(), center);
                if (!best || distance < bestDistance) {
                    best = &candidate;
                    bestDistance = distance;
                }
            }

            if (best) {
                picked.text = best->text;
                picked.box = best->box;
                picked.certain = false;
            }
            return picked;
        }
    }

    // 一个词都没落入选区：不猜。空白处框选就应该什么也不出，
    // 宁可让用户重框一次，也不返回一个中心最近的词。
    return picked;
}
