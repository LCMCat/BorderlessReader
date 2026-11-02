#pragma once

#include <QPoint>
#include <QVector>
#include <QWidget>

class QCloseEvent;
class QContextMenuEvent;
class QMouseEvent;
class QPaintEvent;
class QScreen;
class QVBoxLayout;
class PluginManager;
class ToolCard;

/**
 * @brief 始终置顶的纵向悬浮工具条。
 *
 * 视觉上延续参考图右上角浮窗的“彩色圆角工具卡”风格：每张卡竖向排列，
 * 卡片本身就是工具按钮。按住卡片拖动时不会触发按钮，而是拖动整个悬浮条；
 * 松手后若靠近当前屏幕边缘，会自动吸附并保证窗口完整落在屏幕可用区内。
 *
 * 架构约束：
 *  - 窗口标志只在这里调用一次 setWindowFlags()，全部按位或；
 *  - 本类只处理 UI/拖动/吸附，不直接创建选区器，框选流程交给 PluginManager；
 *  - 全局屏幕坐标的查找/吸附规则放在 ScreenUtils，便于复用和测试。
 */
class FloatBar final : public QWidget
{
    Q_OBJECT

public:
    explicit FloatBar(PluginManager *pluginManager, QWidget *parent = nullptr);
    ~FloatBar() override;

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void buildUi();
    void moveToTopRight(QScreen *screen);
    void showContextMenu(const QPoint &globalPosition);

    void activatePlugin(int index);
    void beginWindowDrag(const QPoint &globalPosition);
    void updateWindowDrag(const QPoint &globalPosition);
    bool finishWindowDrag();
    void snapToEdgeOfCurrentScreen();

    ToolCard *cardForObject(QObject *object) const;

    PluginManager *m_pluginManager = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QVector<ToolCard *> m_cards;

    QPoint m_dragPressGlobal;
    QPoint m_dragStartWindowPosition;
    bool m_dragPressed = false;
    bool m_dragMoved = false;
    bool m_cursorOverridden = false;
};
