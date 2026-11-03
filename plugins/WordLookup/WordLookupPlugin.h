#pragma once

#include "core/IToolPlugin.h"
#include "core/Ocr/TextRecognizer.h"

#include <QPointer>
#include <QStringList>

#include <memory>

class LookupDialog;
class LookupService;

/**
 * @brief 需求 2：即时查词与一词多义辨析。
 *
 * 链路：框选 → core/Ocr 识别（带整行上下文）→ WordPicker 选出用户框住的词
 *       → LookupService 查本地词库 → LookupDialog 展示。
 *
 * 插件只做编排和策略，不自己去抓屏、不自己起线程、不自己解析词库格式。
 */
class WordLookupPlugin final : public IToolPlugin
{
public:
    WordLookupPlugin();
    ~WordLookupPlugin() override;

    QString name() const override;
    QIcon icon() const override;
    void onSelection(const Selection &selection) override;

private:
    void ensureService();
    void openDialog(const QRect &anchor);
    void requestLookup(const QString &word, bool uncertain);
    void rememberWord(const QString &word);
    void startRecognition(const Selection &selection);

    Ocr::Recognizer m_recognizer;
    Ocr::Recognizer::RequestId m_ocrRequest = 0;

    std::unique_ptr<LookupService> m_service;
    bool m_serviceChecked = false;
    QString m_serviceError;

    QPointer<LookupDialog> m_dialog;
    QStringList m_history;
};
