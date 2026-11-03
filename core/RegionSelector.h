#pragma once

#include "core/Selection.h"
#include "core/Utils/CaptureUtils.h"

#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QWidget>

class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

/**
 * @brief 覆盖整个虚拟桌面的全屏框选层。
 *
 * 工作流程：
 *  1. 对虚拟桌面截图，生成灰度暗化底图和彩色原图；
 *  2. 全屏显示无边框置顶窗口，鼠标变为十字；
 *  3. 用户拖拽时实时显示彩色选区、尺寸和十字辅助线；
 *  4. 松开左键后通过 regionSelected 发出全局逻辑坐标 + 原始像素截图；
 *  5. Esc / 右键 / 关闭窗口都通过 canceled 通知调用者恢复悬浮条。
 *
 * 注意：本类不隐藏悬浮条。隐藏来源窗口由 PluginManager 在更早的阶段完成，
 * 这样截图里不会出现悬浮条。
 */
class RegionSelector final : public QWidget
{
    Q_OBJECT

public:
    explicit RegionSelector(QWidget *parent = nullptr);
    ~RegionSelector() override;

    /**
     * 截图并显示选区器。
     * 必须在连接 regionSelected / canceled 之后调用。
     * @return 截图或窗口初始化失败时返回 false。
     */
    bool beginSelection();

signals:
    /// 选区完成。除了选区截图，还带上整屏快照供需要上下文的插件使用。
    void selectionReady(const Selection &selection);
    void canceled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QRect currentSelection() const;
    QPoint clampToVirtualDesktop(const QPoint &globalPoint) const;
    void finishWithSelection();
    void cancelSelection();
    void releaseInputGrab();
    void paintInstructions(QPainter &painter) const;
    void paintSelectionChrome(QPainter &painter, const QRect &localSelection) const;

    DesktopCapture m_capture;
    QRect m_virtualRect;
    QRect m_instructionArea; ///< 鼠标所在屏幕的局部区域，用于多屏时居中提示
    QPixmap m_dimmedPixmap;
    QPixmap m_colorPixmap;

    QPoint m_anchorGlobal;
    QPoint m_cursorGlobal;
    bool m_hasCursor = false;
    bool m_selecting = false;
    bool m_finished = false;
};
