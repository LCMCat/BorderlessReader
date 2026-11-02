#include "shell/FloatBar.h"

#include "core/Utils/ScreenUtils.h"
#include "shell/PluginManager.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace
{
constexpr int kCardWidth = 84;
constexpr int kCardHeight = 126;
constexpr int kBarWidth = 120;
constexpr int kCornerRadius = 25;

struct CardPalette
{
    QColor top;
    QColor bottom;
    QColor ink;
    QColor accent;
    bool dark = false;
};

// 参考图右上角浮窗的奶油白、深海军蓝、雾蓝、薄荷绿、香芋紫五色循环。
// 注册插件时不需要关心配色，新增插件会自动拿到下一种颜色。
CardPalette paletteForIndex(int index)
{
    static const std::array<CardPalette, 5> palettes = {{
        {QColor(0xFF, 0xFA, 0xEC), QColor(0xF1, 0xE0, 0xBB),
         QColor(0x3D, 0x33, 0x23), QColor(0xD2, 0xA3, 0x4A), false},
        {QColor(0x35, 0x44, 0x5D), QColor(0x1A, 0x24, 0x37),
         QColor(0xFF, 0xFB, 0xF1), QColor(0x7E, 0xA9, 0xFF), true},
        {QColor(0xF0, 0xF6, 0xFF), QColor(0xDA, 0xE8, 0xFC),
         QColor(0x1D, 0x35, 0x57), QColor(0x5C, 0x8E, 0xE8), false},
        {QColor(0xF0, 0xFF, 0xF7), QColor(0xD9, 0xF3, 0xE5),
         QColor(0x19, 0x38, 0x2D), QColor(0x44, 0xBC, 0x8A), false},
        {QColor(0xF5, 0xF1, 0xFF), QColor(0xE5, 0xDD, 0xFF),
         QColor(0x2D, 0x24, 0x4B), QColor(0x91, 0x76, 0xE8), false},
    }};

    return palettes[static_cast<std::size_t>(index) % palettes.size()];
}

QColor withAlpha(const QColor &color, int alpha)
{
    QColor result = color;
    result.setAlpha(alpha);
    return result;
}
} // namespace

// ---------------------------------------------------------------------------
// 单张工具卡：只负责绘制和悬停/按下反馈，点击与拖动统一由 FloatBar 过滤。
// ---------------------------------------------------------------------------
class ToolCard final : public QWidget
{
public:
    ToolCard(int index,
             const QString &title,
             const QIcon &icon,
             const CardPalette &palette,
             QWidget *parent)
        : QWidget(parent)
        , m_index(index)
        , m_title(title)
        , m_icon(icon)
        , m_palette(palette)
    {
        setFixedSize(kCardWidth, kCardHeight);
        setCursor(Qt::PointingHandCursor);
        setMouseTracking(true);

        // 透明父窗口上的柔和投影，是参考图卡片“浮起来”的关键。
        auto *shadow = new QGraphicsDropShadowEffect(this);
        shadow->setBlurRadius(28.0);
        shadow->setOffset(0.0, 8.0);
        shadow->setColor(QColor(15, 22, 34, 85));
        setGraphicsEffect(shadow);
    }

    int index() const { return m_index; }

    void setPressed(bool pressed)
    {
        if (m_pressed == pressed)
            return;
        m_pressed = pressed;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QRectF cardRect = rect().adjusted(1.0, 1.0, -1.0, -1.0);
        if (m_pressed)
            cardRect.translate(0.0, 1.0);

        QPainterPath cardPath;
        cardPath.addRoundedRect(cardRect, kCornerRadius, kCornerRadius);

        QLinearGradient background(cardRect.topLeft(), cardRect.bottomLeft());
        background.setColorAt(0.0, m_palette.top);
        background.setColorAt(1.0, m_palette.bottom);
        painter.fillPath(cardPath, background);

        // 顶部高光让卡片更接近参考图的玻璃/哑光质感。
        painter.save();
        painter.setClipPath(cardPath);
        QLinearGradient gloss(cardRect.topLeft(),
                              QPointF(cardRect.left(), cardRect.top() + cardRect.height() * 0.52));
        gloss.setColorAt(0.0, QColor(255, 255, 255, m_palette.dark ? 34 : 128));
        gloss.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillRect(cardRect, gloss);
        painter.restore();

        QColor borderColor = withAlpha(m_palette.dark ? QColor(Qt::white)
                                                      : QColor(0x4F, 0x45, 0x35),
                                       m_hover ? 125 : 55);
        if (m_hover || m_pressed) {
            borderColor = withAlpha(m_palette.accent, 190);
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(borderColor, m_hover ? 1.5 : 1.0));
        painter.drawPath(cardPath);

        if (m_pressed) {
            painter.save();
            painter.setClipPath(cardPath);
            painter.fillRect(cardRect, QColor(0, 0, 0, 10));
            painter.restore();
        }

        // 左上角小圆徽标与右上角在线点，呼应参考图的角标细节。
        const QRectF badgeRect(13.0, 13.0, 18.0, 18.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_palette.accent);
        painter.drawEllipse(badgeRect);

        const QPointF badgeCenter = badgeRect.center();
        QPainterPath sparkle;
        const qreal radius = 4.5;
        sparkle.moveTo(badgeCenter.x(), badgeCenter.y() - radius);
        sparkle.quadTo(badgeCenter.x() + 1.1, badgeCenter.y() - 1.1,
                       badgeCenter.x() + radius, badgeCenter.y());
        sparkle.quadTo(badgeCenter.x() + 1.1, badgeCenter.y() + 1.1,
                       badgeCenter.x(), badgeCenter.y() + radius);
        sparkle.quadTo(badgeCenter.x() - 1.1, badgeCenter.y() + 1.1,
                       badgeCenter.x() - radius, badgeCenter.y());
        sparkle.quadTo(badgeCenter.x() - 1.1, badgeCenter.y() - 1.1,
                       badgeCenter.x(), badgeCenter.y() - radius);
        painter.setBrush(m_palette.dark ? QColor(0x22, 0x2C, 0x3C)
                                        : QColor(0x3B, 0x32, 0x23));
        painter.drawPath(sparkle);

        painter.setBrush(withAlpha(m_palette.accent, 210));
        painter.drawEllipse(QPointF(cardRect.right() - 16.0, 17.0), 2.6, 2.6);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
        painter.drawEllipse(QPointF(cardRect.right() - 16.0, 17.0), 4.2, 4.2);

        // 白色圆形图标底座。
        const QPointF tileCenter(cardRect.center().x(), cardRect.top() + 50.0);
        const qreal tileRadius = 23.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(8, 13, 20, 18));
        painter.drawEllipse(tileCenter + QPointF(0.0, 2.0), tileRadius, tileRadius);

        QLinearGradient tileGradient(tileCenter + QPointF(-tileRadius, -tileRadius),
                                     tileCenter + QPointF(tileRadius, tileRadius));
        tileGradient.setColorAt(0.0, QColor(255, 255, 255, 245));
        tileGradient.setColorAt(1.0, QColor(248, 245, 238, 228));
        painter.setBrush(tileGradient);
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1.0));
        painter.drawEllipse(tileCenter, tileRadius, tileRadius);

        const QRect iconRect(qRound(tileCenter.x() - 13.0),
                             qRound(tileCenter.y() - 13.0),
                             26,
                             26);
        m_icon.paint(&painter, iconRect, Qt::AlignCenter,
                     m_hover ? QIcon::Active : QIcon::Normal, QIcon::Off);

        QFont titleFont = font();
        titleFont.setPixelSize(12);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);

        const QColor titleColor = m_palette.ink;
        painter.setPen(titleColor);
        const QRect titleRect(7, 84, width() - 14, 18);
        const QFontMetrics titleMetrics(titleFont);
        painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter,
                         titleMetrics.elidedText(m_title, Qt::ElideRight, titleRect.width()));

        QFont subtitleFont = font();
        subtitleFont.setPixelSize(9);
        subtitleFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.6);
        painter.setFont(subtitleFont);
        painter.setPen(withAlpha(titleColor, 150));
        painter.drawText(QRect(7, 102, width() - 14, 15),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         QStringLiteral("单击框选"));

        // 底部的极细强调线，提供参考图卡片下部装饰的轻量版本。
        painter.setPen(QPen(withAlpha(m_palette.accent, m_hover ? 210 : 130),
                            m_hover ? 2.0 : 1.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(width() / 2.0 - 11.0, height() - 8.0),
                         QPointF(width() / 2.0 + 11.0, height() - 8.0));
    }

    void enterEvent(QEnterEvent *event) override
    {
        m_hover = true;
        update();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        update();
        QWidget::leaveEvent(event);
    }

private:
    int m_index = 0;
    QString m_title;
    QIcon m_icon;
    CardPalette m_palette;
    bool m_hover = false;
    bool m_pressed = false;
};

// ---------------------------------------------------------------------------
// FloatBar
// ---------------------------------------------------------------------------
FloatBar::FloatBar(PluginManager *pluginManager, QWidget *parent)
    : QWidget(parent)
    , m_pluginManager(pluginManager)
{
    // 关键约束：窗口标志一次性按位或设置，之后绝不再调用 setWindowFlags()。
    setWindowFlags(Qt::FramelessWindowHint
                   | Qt::Tool
                   | Qt::WindowStaysOnTopHint
                   | Qt::NoDropShadowWindowHint);

    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_AlwaysShowToolTips, true);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setWindowTitle(tr("阅界 - 悬浮工具条"));

    buildUi();
    moveToTopRight(QGuiApplication::primaryScreen());

    if (m_pluginManager) {
        connect(m_pluginManager, &PluginManager::selectionFailed,
                this, [this](const QString &message) {
            if (!isVisible())
                show();
            QString title = tr("无法开始框选");
            QMessageBox::warning(this, title, message, QMessageBox::Ok);
        });
    }
}

FloatBar::~FloatBar()
{
    if (m_cursorOverridden) {
        QGuiApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }
}

void FloatBar::buildUi()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(18, 34, 18, 22);
    m_layout->setSpacing(12);
    m_layout->setAlignment(Qt::AlignHCenter);

    const int pluginCount = m_pluginManager ? m_pluginManager->count() : 0;
    for (int index = 0; index < pluginCount; ++index) {
        IToolPlugin *plugin = m_pluginManager->plugin(index);
        if (!plugin)
            continue;

        auto *card = new ToolCard(index, plugin->name(), plugin->icon(),
                                  paletteForIndex(index), this);
        card->setAccessibleName(plugin->name());
        card->setToolTip(tr("%1\n单击开始框选 · 按住卡片可拖动悬浮条")
                             .arg(plugin->name()));
        card->installEventFilter(this);

        m_cards.push_back(card);
        m_layout->addWidget(card, 0, Qt::AlignHCenter);
    }

    if (m_cards.isEmpty()) {
        auto *empty = new QLabel(tr("暂无插件\n右键退出"), this);
        empty->setAlignment(Qt::AlignCenter);
        empty->setFixedSize(kCardWidth, 92);
        empty->setAttribute(Qt::WA_TransparentForMouseEvents);
        empty->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  color: #5E6673;"
            "  background: rgba(255, 250, 240, 215);"
            "  border: 1px solid rgba(255, 255, 255, 160);"
            "  border-radius: 20px;"
            "  font-size: 12px;"
            "}"
        ));
        m_layout->addWidget(empty, 0, Qt::AlignHCenter);
    }

    setFixedWidth(kBarWidth);
    adjustSize();
}

void FloatBar::moveToTopRight(QScreen *screen)
{
    QScreen *target = screen;
    if (!target) {
        target = QGuiApplication::primaryScreen();
    }
    if (!target)
        return;

    const QRect available = target->availableGeometry();
    if (!available.isValid())
        return;

    const int x = available.right() - width() + 1 - 18;
    const int y = available.top() + 22;
    move(x, y);
}

void FloatBar::activatePlugin(int index)
{
    if (!m_pluginManager)
        return;
    m_pluginManager->requestRegion(index, this);
}

ToolCard *FloatBar::cardForObject(QObject *object) const
{
    for (ToolCard *card : m_cards) {
        if (card == object)
            return card;
    }
    return nullptr;
}

bool FloatBar::eventFilter(QObject *watched, QEvent *event)
{
    ToolCard *card = cardForObject(watched);
    if (!card)
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            beginWindowDrag(mouseEvent->globalPosition().toPoint());
            card->setPressed(true);
            return true;
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (m_dragPressed) {
            updateWindowDrag(mouseEvent->globalPosition().toPoint());
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton && m_dragPressed) {
            const bool wasMoved = m_dragMoved;
            finishWindowDrag();
            card->setPressed(false);

            // 移动过就是拖拽；没移动过才是点击，避免拖动悬浮条时误触工具。
            if (!wasMoved)
                activatePlugin(card->index());
            return true;
        }
        break;
    }
    case QEvent::ContextMenu: {
        auto *contextEvent = static_cast<QContextMenuEvent *>(event);
        showContextMenu(contextEvent->globalPos());
        return true;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void FloatBar::beginWindowDrag(const QPoint &globalPosition)
{
    m_dragPressed = true;
    m_dragMoved = false;
    m_dragPressGlobal = globalPosition;
    m_dragStartWindowPosition = pos();
}

void FloatBar::updateWindowDrag(const QPoint &globalPosition)
{
    if (!m_dragPressed)
        return;

    const QPoint delta = globalPosition - m_dragPressGlobal;

    if (!m_dragMoved && delta.manhattanLength() >= QApplication::startDragDistance()) {
        m_dragMoved = true;
        if (!m_cursorOverridden) {
            QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
            m_cursorOverridden = true;
        }
    }

    // 使用“按下时的窗口位置 + 全局鼠标位移”，而不是窗口内局部坐标，
    // 这样跨屏拖动时不会因为 DPR 变化产生累计误差。
    if (m_dragMoved)
        move(m_dragStartWindowPosition + delta);
}

bool FloatBar::finishWindowDrag()
{
    if (!m_dragPressed)
        return false;

    const bool wasMoved = m_dragMoved;
    m_dragPressed = false;
    m_dragMoved = false;

    if (m_cursorOverridden) {
        QGuiApplication::restoreOverrideCursor();
        m_cursorOverridden = false;
    }

    if (wasMoved)
        snapToEdgeOfCurrentScreen();

    return wasMoved;
}

void FloatBar::snapToEdgeOfCurrentScreen()
{
    // 使用窗口几何与每块屏幕的相交面积判断归属；多屏时不会一直吸附到主屏。
    QScreen *screen = ScreenUtils::screenForRect(geometry());
    if (!screen)
        return;

    const QRect snapped = ScreenUtils::snapToScreenEdges(
        geometry(), screen, ScreenUtils::edgeSnapThreshold());

    if (snapped.topLeft() != pos())
        move(snapped.topLeft());
    raise();
}

void FloatBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        beginWindowDrag(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void FloatBar::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragPressed) {
        updateWindowDrag(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    QWidget::mouseMoveEvent(event);
}

void FloatBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragPressed) {
        finishWindowDrag();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void FloatBar::contextMenuEvent(QContextMenuEvent *event)
{
    showContextMenu(event->globalPos());
    event->accept();
}

void FloatBar::closeEvent(QCloseEvent *event)
{
    // 应用设置了 quitOnLastWindowClosed(false)（框选时主窗口会短暂隐藏），
    // 因此 Alt+F4 关闭悬浮条时要显式退出，避免留下一个没有界面的后台进程。
    QCoreApplication::quit();
    QWidget::closeEvent(event);
}

void FloatBar::showContextMenu(const QPoint &globalPosition)
{
    QMenu menu;
    menu.setAttribute(Qt::WA_TranslucentBackground, false);
    menu.setStyleSheet(QStringLiteral(
        "QMenu {"
        "  background: #1E2837;"
        "  color: #F6F1E7;"
        "  border: 1px solid #3A4658;"
        "  border-radius: 10px;"
        "  padding: 6px;"
        "  font-size: 12px;"
        "}"
        "QMenu::item {"
        "  padding: 8px 26px 8px 14px;"
        "  border-radius: 7px;"
        "}"
        "QMenu::item:selected {"
        "  background: #D2A34A;"
        "  color: #201B12;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  background: #3A4658;"
        "  margin: 5px 8px;"
        "}"
    ));

    QAction *snapAction = menu.addAction(tr("吸附到当前屏幕右上角"));
    menu.addSeparator();
    QAction *quitAction = menu.addAction(tr("退出阅界"));

    QAction *chosen = menu.exec(globalPosition);

    if (chosen == snapAction) {
        QScreen *screen = ScreenUtils::screenAtPoint(QCursor::pos(),
                                                     ScreenUtils::screenForRect(geometry()));
        moveToTopRight(screen);
        raise();
    } else if (chosen == quitAction) {
        QCoreApplication::quit();
    }
}

void FloatBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 顶部的磨砂拖拽把手。悬浮条其余区域完全透明，视觉焦点留给卡片。
    const QRectF grip((width() - 42.0) / 2.0, 9.0, 42.0, 15.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(8, 12, 18, 25));
    painter.drawRoundedRect(grip.translated(0.0, 2.0), 7.5, 7.5);

    painter.setPen(QPen(QColor(255, 255, 255, 135), 1.0));
    painter.setBrush(QColor(255, 251, 244, 218));
    painter.drawRoundedRect(grip, 7.5, 7.5);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(68, 73, 82, 130));
    for (int i = -1; i <= 1; ++i) {
        painter.drawEllipse(QPointF(grip.center().x() + i * 6.0, grip.center().y()),
                            1.4, 1.4);
    }
}
