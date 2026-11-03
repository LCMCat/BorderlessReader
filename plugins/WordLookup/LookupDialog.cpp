#include "plugins/WordLookup/LookupDialog.h"

#include "core/Utils/ScreenUtils.h"

#include <QClipboard>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>

namespace
{
constexpr int kHistoryLimit = 5;
constexpr int kDialogWidth = 430;

QString dialogStyleSheet()
{
    return QStringLiteral(
        "QDialog#LookupDialog { background: transparent; }"
        "QFrame#card { background: #F6F2E9; border: 1px solid #E4DCCB; border-radius: 18px; }"
        "QLabel { font-family: 'Microsoft YaHei UI', 'Microsoft YaHei', 'Segoe UI'; color: #202A39; }"
        "QLabel#eyebrow { color: #9A7A38; font-size: 10px; font-weight: 600; }"
        "QLabel#chip { color: #654B17; background: #F0E0B7; border: 1px solid #E5D19E;"
        "  border-radius: 9px; padding: 4px 9px; font-size: 9px; font-weight: 600; }"
        "QLineEdit#wordField {"
        "  background: transparent; border: none; color: #1D2736;"
        "  font-size: 26px; font-weight: 600; padding: 0px; }"
        "QLabel#phonetic { color: #7B818A; font-size: 12px;"
        "  font-family: 'Cascadia Mono', 'Consolas', monospace; }"
        "QFrame#bodyCard { background: #FFFDF8; border: 1px solid #E6DFD1; border-radius: 16px; }"
        "QScrollArea { background: transparent; border: none; }"
        "QWidget#senseHost { background: transparent; }"
        "QFrame#senseCard { background: #FFFFFF; border: 1px solid #EDE7DB; border-radius: 12px; }"
        "QLabel#partOfSpeech { color: #5A4A20; background: #F3E7C6; border-radius: 7px;"
        "  padding: 2px 8px; font-size: 9px; font-weight: 700; }"
        "QLabel#definition { color: #222C3B; font-size: 12px; }"
        "QLabel#example { color: #7B818A; font-size: 11px; font-style: italic; }"
        "QLabel#inflections { color: #8A877E; font-size: 10px; }"
        "QLabel#status { color: #8A877E; font-size: 10px; }"
        "QLabel#emptyHint { color: #A19B8F; font-size: 11px; }"
        "QLabel#historyTitle { color: #8A877E; font-size: 10px; }"
        "QPushButton#historyChip { color: #3A4B62; background: #ECE6DA; border: 1px solid #E0D7C6;"
        "  border-radius: 9px; padding: 3px 10px; font-size: 10px; }"
        "QPushButton#historyChip:hover { background: #E2DACB; }"
        "QPushButton#actionButton { color: #FFF8EA; background: #1F2B3C; border: none;"
        "  border-radius: 10px; padding: 8px 20px; font-size: 12px; font-weight: 600; }"
        "QPushButton#actionButton:hover { background: #2D3E56; }"
        "QPushButton#actionButton:disabled { background: #B9BDC4; }");
}
} // namespace

LookupDialog::LookupDialog(const QString &initialWord,
                           const QIcon &windowIcon,
                           const QStringList &history,
                           bool uncertain,
                           QWidget *parent)
    : QDialog(parent)
    , m_history(history)
    , m_uncertain(uncertain)
{
    setObjectName(QStringLiteral("LookupDialog"));
    setWindowTitle(tr("即时查词"));
    setWindowIcon(windowIcon);
    setAttribute(Qt::WA_DeleteOnClose, true);

    // 浮窗形态：无边框、置顶、不占任务栏。
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setStyleSheet(dialogStyleSheet());
    setFixedWidth(kDialogWidth);

    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));

    // 不用 QGraphicsDropShadowEffect：它的包围盒会超出窗口边界，
    // 分层窗口（WA_TranslucentBackground）下 Windows 会拒绝
    // UpdateLayeredWindowIndirect，边缘出现裁切。
    auto *frameLayout = new QVBoxLayout(this);
    frameLayout->setContentsMargins(10, 10, 10, 12);
    frameLayout->setSpacing(0);
    frameLayout->addWidget(card);

    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(20, 16, 20, 14);
    outer->setSpacing(11);

    // 顶栏：图标 + 可编辑的词 + 音标。
    auto *header = new QHBoxLayout;
    header->setSpacing(12);

    auto *iconTile = new QLabel(this);
    iconTile->setFixedSize(46, 46);
    iconTile->setAlignment(Qt::AlignCenter);
    iconTile->setPixmap(windowIcon.pixmap(26, 26));
    iconTile->setStyleSheet(QStringLiteral(
        "QLabel { background: #FFFFFF; border: 1px solid #E8E1D3; border-radius: 14px; }"));
    header->addWidget(iconTile, 0, Qt::AlignVCenter);

    auto *titleColumn = new QVBoxLayout;
    titleColumn->setContentsMargins(0, 0, 0, 0);
    titleColumn->setSpacing(1);

    auto *eyebrow = new QLabel(tr("WORD LOOKUP"), this);
    eyebrow->setObjectName(QStringLiteral("eyebrow"));
    titleColumn->addWidget(eyebrow);

    m_wordField = new QLineEdit(this);
    m_wordField->setObjectName(QStringLiteral("wordField"));
    m_wordField->setPlaceholderText(tr("输入英文单词"));
    m_wordField->setClearButtonEnabled(false);
    titleColumn->addWidget(m_wordField);

    m_phonetic = new QLabel(this);
    m_phonetic->setObjectName(QStringLiteral("phonetic"));
    m_phonetic->setText(QString());
    titleColumn->addWidget(m_phonetic);

    m_inflections = new QLabel(this);
    m_inflections->setObjectName(QStringLiteral("inflections"));
    m_inflections->setWordWrap(true);
    m_inflections->hide();
    titleColumn->addWidget(m_inflections);

    header->addLayout(titleColumn, 1);

    auto *chip = new QLabel(tr("一词多义"), this);
    chip->setObjectName(QStringLiteral("chip"));
    header->addWidget(chip, 0, Qt::AlignTop);
    outer->addLayout(header);

    // 正文：义项列表放在可滚动区域里，避免长词条把窗口撑出屏幕。
    auto *bodyCard = new QFrame(this);
    bodyCard->setObjectName(QStringLiteral("bodyCard"));
    auto *bodyLayout = new QVBoxLayout(bodyCard);
    bodyLayout->setContentsMargins(14, 12, 14, 14);
    bodyLayout->setSpacing(8);

    m_scroll = new QScrollArea(bodyCard);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMinimumHeight(230);
    m_scroll->setMaximumHeight(380);

    auto *senseHost = new QWidget(m_scroll);
    senseHost->setObjectName(QStringLiteral("senseHost"));
    m_senseLayout = new QVBoxLayout(senseHost);
    m_senseLayout->setContentsMargins(0, 0, 0, 0);
    m_senseLayout->setSpacing(10);
    m_scroll->setWidget(senseHost);

    bodyLayout->addWidget(m_scroll);
    outer->addWidget(bodyCard, 1);

    // 查询历史：最多 5 条。
    auto *historyRow = new QHBoxLayout;
    historyRow->setSpacing(8);
    auto *historyTitle = new QLabel(tr("最近查询"), this);
    historyTitle->setObjectName(QStringLiteral("historyTitle"));
    historyRow->addWidget(historyTitle, 0, Qt::AlignVCenter);

    m_historyLayout = new QHBoxLayout;
    m_historyLayout->setSpacing(6);
    historyRow->addLayout(m_historyLayout, 1);
    historyRow->addStretch(0);
    outer->addLayout(historyRow);

    // 底栏：状态提示 + 复制 + 关闭。
    auto *footer = new QHBoxLayout;
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("status"));
    footer->addWidget(m_status, 1, Qt::AlignVCenter);

    m_copyButton = new QPushButton(tr("复制释义"), this);
    m_copyButton->setObjectName(QStringLiteral("actionButton"));
    m_copyButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setAutoDefault(false);
    m_copyButton->setEnabled(false);
    footer->addWidget(m_copyButton, 0, Qt::AlignVCenter);

    auto *closeButton = new QPushButton(tr("关闭"), this);
    closeButton->setObjectName(QStringLiteral("actionButton"));
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setAutoDefault(false);
    footer->addWidget(closeButton, 0, Qt::AlignVCenter);
    outer->addLayout(footer);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_copyButton, &QPushButton::clicked, this, [this]() { copyCurrentEntry(); });
    connect(m_wordField, &QLineEdit::returnPressed, this,
            [this]() { requestLookup(m_wordField->text()); });

    rebuildHistory(m_history);
    m_wordField->setText(initialWord);
    showEmptyHint();

    if (!initialWord.isEmpty())
        requestLookup(initialWord);
    else
        showMessage(tr("在上方输入单词后按回车，或重新框选。"));

    adjustSize();
    m_wordField->setFocus();
    // 从 OCR 拿到词时不要全选——满屏蓝色高亮看着像“待覆盖”，
    // 只有等用户手动输入时才全选，方便直接改写。
    if (initialWord.isEmpty())
        m_wordField->selectAll();
    else
        m_wordField->setCursorPosition(m_wordField->text().size());
}

void LookupDialog::setLookupHandler(std::function<void(const QString &)> handler)
{
    m_onLookupRequested = std::move(handler);
}

void LookupDialog::showEntry(const WordEntry &entry, const QString &matchedFrom)
{
    renderEntry(entry, matchedFrom);
}

void LookupDialog::showMessage(const QString &message)
{
    m_status->setText(message);
}

void LookupDialog::setWord(const QString &word)
{
    m_wordField->setText(word);
    m_wordField->deselect();
    m_wordField->setCursorPosition(m_wordField->text().size());
}

void LookupDialog::setUncertain(bool uncertain)
{
    m_uncertain = uncertain;
}

void LookupDialog::settleWordField()
{
    m_wordField->deselect();
    m_wordField->setCursorPosition(m_wordField->text().size());
}

void LookupDialog::enableAutoHide()
{
    m_autoHide = true;
}

void LookupDialog::placeNextTo(const QRect &anchor)
{
    QScreen *screen = ScreenUtils::screenForRect(anchor);
    const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
    const QSize size = this->size();

    int x = anchor.right() + 14;
    if (x + size.width() > available.right())
        x = anchor.left() - size.width() - 14;
    if (x < available.left())
        x = available.right() - size.width() - 14;

    int y = anchor.top();
    if (y + size.height() > available.bottom())
        y = available.bottom() - size.height();
    if (y < available.top())
        y = available.top();

    move(QPoint(x, y));
}

bool LookupDialog::event(QEvent *event)
{
    // 点别处（切到 PDF、点悬浮条、切到别的程序）自动收起浮窗。
    if (event->type() == QEvent::WindowDeactivate && m_autoHide && !m_closing) {
        m_closing = true;
        // 延到下一个事件循环再关，避免在窗口状态切换中途重入。
        QTimer::singleShot(0, this, [this]() { close(); });
    }
    return QDialog::event(event);
}

void LookupDialog::mousePressEvent(QMouseEvent *event)
{
    // 无边框窗口没法拖标题栏，这里让整张卡片都能按住拖走。
    if (event->button() == Qt::LeftButton) {
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QDialog::mousePressEvent(event);
}

void LookupDialog::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && !m_dragOffset.isNull()) {
        move(event->globalPosition().toPoint() - m_dragOffset);
        event->accept();
        return;
    }
    QDialog::mouseMoveEvent(event);
}

void LookupDialog::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_dragOffset = QPoint();
    QDialog::mouseReleaseEvent(event);
}

void LookupDialog::requestLookup(const QString &word)
{
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty()) {
        showMessage(tr("先输入一个英文单词。"));
        return;
    }

    m_copyButton->setEnabled(false);
    showMessage(tr("正在查询 %1 …").arg(trimmed));
    rememberLocally(trimmed);

    if (m_onLookupRequested)
        m_onLookupRequested(trimmed);
}

void LookupDialog::rememberLocally(const QString &word)
{
    const QString normalized = word.toLower();
    m_history.removeAll(normalized);
    m_history.prepend(normalized);
    while (m_history.size() > kHistoryLimit)
        m_history.removeLast();
    rebuildHistory(m_history);
}

void LookupDialog::renderEntry(const WordEntry &entry, const QString &matchedForm)
{
    m_currentEntry = entry;
    clearSenses();
    m_copyButton->setEnabled(true);

    // QLineEdit::setText 在控件有焦点时会顺手全选，弹出来一片蓝底很难看；
    // 这里显式取消选区，把光标放到末尾。
    m_wordField->setText(entry.word.isEmpty() ? matchedForm : entry.word);
    m_wordField->deselect();
    m_wordField->setCursorPosition(m_wordField->text().size());
    m_phonetic->setText(entry.phonetic);

    if (entry.inflections.isEmpty()) {
        m_inflections->hide();
        m_inflections->clear();
    } else {
        m_inflections->setText(tr("变形 · ") + entry.inflections.join(QStringLiteral(" · ")));
        m_inflections->show();
    }

    for (const WordSense &sense : entry.senses) {
        auto *card = new QFrame(m_scroll->widget());
        card->setObjectName(QStringLiteral("senseCard"));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 10, 14, 12);
        cardLayout->setSpacing(6);

        auto *posRow = new QHBoxLayout;
        auto *pos = new QLabel(sense.partOfSpeech, card);
        pos->setObjectName(QStringLiteral("partOfSpeech"));
        posRow->addWidget(pos, 0, Qt::AlignVCenter);
        posRow->addStretch(1);
        cardLayout->addLayout(posRow);

        for (int i = 0; i < sense.definitions.size(); ++i) {
            auto *definition = new QLabel(
                QStringLiteral("%1. %2").arg(i + 1).arg(sense.definitions.at(i)), card);
            definition->setObjectName(QStringLiteral("definition"));
            definition->setWordWrap(true);
            definition->setTextInteractionFlags(Qt::TextSelectableByMouse);
            cardLayout->addWidget(definition);

            const QString example = sense.examples.value(i);
            if (!example.isEmpty()) {
                auto *exampleLabel = new QLabel(tr("例：%1").arg(example), card);
                exampleLabel->setObjectName(QStringLiteral("example"));
                exampleLabel->setWordWrap(true);
                cardLayout->addWidget(exampleLabel);
            }
        }

        m_senseLayout->addWidget(card);
    }

    m_senseLayout->addStretch(1);

    int senseCount = 0;
    for (const WordSense &sense : entry.senses)
        senseCount += static_cast<int>(sense.definitions.size());

    // 不要在这里写“按常用度排序”：数据源不保证频率序，写了就是假话。
    QString status = tr("%1 个词性 · %2 个义项")
                         .arg(static_cast<int>(entry.senses.size()))
                         .arg(senseCount);
    if (m_uncertain)
        status += tr(" · OCR 结果，请核对拼写");
    if (!matchedForm.isEmpty() && matchedForm.compare(entry.word, Qt::CaseInsensitive) != 0)
        status += tr(" · 由 %1 还原").arg(matchedForm);
    showMessage(status);
}

void LookupDialog::showEmptyHint()
{
    clearSenses();
    auto *hint = new QLabel(tr("结果会显示在这里。"), m_scroll->widget());
    hint->setObjectName(QStringLiteral("emptyHint"));
    hint->setWordWrap(true);
    m_senseLayout->addWidget(hint);
    m_senseLayout->addStretch(1);
}

void LookupDialog::clearSenses()
{
    while (QLayoutItem *item = m_senseLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
}

void LookupDialog::rebuildHistory(const QStringList &history)
{
    while (QLayoutItem *item = m_historyLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    if (history.isEmpty()) {
        auto *empty = new QLabel(tr("还没有查过词"), this);
        empty->setObjectName(QStringLiteral("emptyHint"));
        empty->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_historyLayout->addWidget(empty, 0, Qt::AlignVCenter);
        return;
    }

    for (const QString &word : history) {
        auto *chip = new QPushButton(word, this);
        chip->setObjectName(QStringLiteral("historyChip"));
        chip->setCursor(Qt::PointingHandCursor);
        chip->setAutoDefault(false);
        // 不锁尺寸的话 QPushButton 会横向撑满整行，看起来像一条输入框。
        chip->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(chip, &QPushButton::clicked, this, [this, word]() {
            m_wordField->setText(word);
            requestLookup(word);
        });
        m_historyLayout->addWidget(chip, 0, Qt::AlignVCenter);
    }
}

void LookupDialog::copyCurrentEntry()
{
    if (!m_currentEntry.isValid()) {
        showMessage(tr("还没有可复制的释义。"));
        return;
    }
    QGuiApplication::clipboard()->setText(m_currentEntry.toPlainText());
    showMessage(tr("释义已复制到剪贴板。"));
}
