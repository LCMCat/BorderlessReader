#pragma once

#include "plugins/WordLookup/lookup/WordEntry.h"

#include <QDialog>
#include <QIcon>
#include <QPoint>
#include <QRect>
#include <QStringList>

#include <functional>

class QEvent;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMouseEvent;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/**
 * @brief 查词浮窗。
 *
 * 界面与交互原样承自贡献者版本（配色、字号、布局、拖拽、失焦收起、历史、复制），
 * 只把“数据从哪来”抽了出去：窗口不再自己持有 DictionaryClient，而是通过
 * setLookupHandler() 把查询交给插件，再由插件调用 showEntry()/showMessage()
 * 把结果填回来。
 */
class LookupDialog final : public QDialog
{
public:
    LookupDialog(const QString &initialWord,
                 const QIcon &windowIcon,
                 const QStringList &history,
                 bool uncertain,
                 QWidget *parent = nullptr);

    /// 用户按回车或点历史词时回调；真正的查词由插件负责。
    void setLookupHandler(std::function<void(const QString &)> handler);

    /// 显示一条查词结果。matchedFrom 与词条原形不同时，状态栏会标注还原来源。
    void showEntry(const WordEntry &entry, const QString &matchedFrom);

    /// 显示一条状态提示（识别中 / 未收录 / 词库不可用）。
    void showMessage(const QString &message);

    /// 把识别到的词放进输入框，但不触发查询。
    void setWord(const QString &word);

    /// OCR 来源的词会在状态栏提示核对拼写；手动输入时关掉。
    void setUncertain(bool uncertain);

    void settleWordField();
    void enableAutoHide();

    /// 把浮窗摆在选区旁边：优先右侧，放不下改左侧，两侧都放不下就贴屏幕右边。
    void placeNextTo(const QRect &anchor);

protected:
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void requestLookup(const QString &word);
    void rememberLocally(const QString &word);
    void renderEntry(const WordEntry &entry, const QString &matchedForm);
    void showEmptyHint();
    void clearSenses();
    void rebuildHistory(const QStringList &history);
    void copyCurrentEntry();

    QLineEdit *m_wordField = nullptr;
    QLabel *m_phonetic = nullptr;
    QLabel *m_inflections = nullptr;
    QLabel *m_status = nullptr;
    QScrollArea *m_scroll = nullptr;
    QVBoxLayout *m_senseLayout = nullptr;
    QHBoxLayout *m_historyLayout = nullptr;
    QPushButton *m_copyButton = nullptr;

    WordEntry m_currentEntry;
    QStringList m_history;
    std::function<void(const QString &)> m_onLookupRequested;
    QPoint m_dragOffset;
    bool m_autoHide = false;
    bool m_closing = false;
    bool m_uncertain = false;
};
