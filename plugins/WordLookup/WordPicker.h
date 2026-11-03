#pragma once

#include "core/Ocr/TextRecognizer.h"

#include <QRectF>
#include <QString>
#include <QVector>

/**
 * @brief 把 OCR 的词框裁决成“用户框住的那个词”。
 *
 * 这是**插件策略**，不是 OCR 机制：core/Ocr 只给词和词框，选哪个词由这里决定。
 * 规则刻意保守：
 *  - 选区里只落进一个词 → 采信；
 *  - 落进多个词 → 优先取“选区中心落在其框内”的那个，否则取中心最近的，标记不确定；
 *  - 一个都没落进来 → 返回空：空白处框选就应该什么也不出，绝不返回“最近的词”。
 * 不确定只影响 UI 提示，不做自动拼写纠正。
 */
namespace WordPicker
{
struct PickedWord
{
    QString text;
    QRectF box;
    bool certain = false;
};

/// selection 是选区在图片像素坐标中的矩形（与 OCR 词框同一坐标系）。
PickedWord pick(const QVector<Ocr::Word> &words, const QRectF &selection);
} // namespace WordPicker
