#pragma once

#include "core/IToolPlugin.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <vector>

class QWidget;
class RegionSelector;

/**
 * @brief 插件注册表和框选流程协调器。
 *
 * 插件在编译期通过 addPlugin() 注册，不使用 QPluginLoader。点击悬浮条时：
 *   1. 隐藏来源窗口（悬浮条），给桌面合成器一点时间；
 *   2. 创建全屏 RegionSelector；
 *   3. 选区完成/取消后恢复悬浮条；
 *   4. 把 QRect + QPixmap 直接交给对应插件的 onRegionSelected()。
 *
 * 插件对象的生命周期由 PluginManager 自己持有，界面层不负责 delete。
 */
class PluginManager final : public QObject
{
    Q_OBJECT

public:
    explicit PluginManager(QObject *parent = nullptr);
    ~PluginManager() override;

    /// 注册插件；传空指针会被忽略。调用顺序决定悬浮条中的显示顺序。
    void addPlugin(std::unique_ptr<IToolPlugin> plugin);

    int count() const;
    IToolPlugin *plugin(int index) const;

    /**
     * 开始一次框选。
     * @param pluginIndex 要接收结果的插件下标
     * @param sourceWidget 需要暂时隐藏的悬浮条；可以为 nullptr
     */
    void requestRegion(int pluginIndex, QWidget *sourceWidget = nullptr);

signals:
    /// 截图/选区器无法启动时通知界面层。此时来源窗口已经恢复。
    void selectionFailed(const QString &message);

private:
    void startCaptureAfterHide();
    void restoreSourceWidget();

    std::vector<std::unique_ptr<IToolPlugin>> m_plugins;
    QPointer<QWidget> m_sourceWidget;
    QPointer<RegionSelector> m_selector;
    QTimer m_captureDelay;

    int m_pendingPluginIndex = -1;
    bool m_active = false;
};
