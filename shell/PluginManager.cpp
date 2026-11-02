#include "shell/PluginManager.h"

#include "core/RegionSelector.h"

#include <QPixmap>
#include <QWidget>

PluginManager::PluginManager(QObject *parent)
    : QObject(parent)
{
    m_captureDelay.setSingleShot(true);
    m_captureDelay.setInterval(100);

    connect(&m_captureDelay, &QTimer::timeout,
            this, &PluginManager::startCaptureAfterHide);
}

PluginManager::~PluginManager() = default;

void PluginManager::addPlugin(std::unique_ptr<IToolPlugin> plugin)
{
    if (plugin)
        m_plugins.push_back(std::move(plugin));
}

int PluginManager::count() const
{
    return static_cast<int>(m_plugins.size());
}

IToolPlugin *PluginManager::plugin(int index) const
{
    if (index < 0 || index >= count())
        return nullptr;
    return m_plugins[static_cast<std::size_t>(index)].get();
}

void PluginManager::requestRegion(int pluginIndex, QWidget *sourceWidget)
{
    if (m_active || !plugin(pluginIndex))
        return;

    m_active = true;
    m_pendingPluginIndex = pluginIndex;
    m_sourceWidget = sourceWidget;

    // 抓屏前必须先藏起悬浮条，否则它会被固化到截图里。延迟一帧以上，
    // 确保 Windows DWM 已经完成窗口隐藏后的桌面合成。
    if (m_sourceWidget)
        m_sourceWidget->hide();

    m_captureDelay.start();
}

void PluginManager::startCaptureAfterHide()
{
    if (!m_active)
        return;

    auto *selector = new RegionSelector;
    m_selector = selector;

    connect(selector, &RegionSelector::regionSelected,
            this, [this, selector](const QRect &globalRect, const QPixmap &regionPixmap) {
        if (m_selector != selector)
            return;

        m_selector = nullptr;
        m_active = false;

        const int pluginIndex = m_pendingPluginIndex;
        m_pendingPluginIndex = -1;

        // 先恢复基础设施窗口，再执行业务回调；Demo 窗口可以自然盖在最上面。
        restoreSourceWidget();

        if (IToolPlugin *tool = plugin(pluginIndex))
            tool->onRegionSelected(globalRect, regionPixmap);
    });

    connect(selector, &RegionSelector::canceled,
            this, [this, selector]() {
        if (m_selector != selector)
            return;

        m_selector = nullptr;
        m_active = false;
        m_pendingPluginIndex = -1;
        restoreSourceWidget();
    });

    // 兜底：哪怕选区器被外部意外销毁，也不能让悬浮条一直消失、
    // 或让 m_active 永久卡死。
    connect(selector, &QObject::destroyed,
            this, [this, selector]() {
        if (m_selector != selector)
            return;

        m_selector = nullptr;
        m_active = false;
        m_pendingPluginIndex = -1;
        restoreSourceWidget();
    });

    if (!selector->beginSelection()) {
        m_selector = nullptr;
        m_active = false;
        m_pendingPluginIndex = -1;
        restoreSourceWidget();
        selector->deleteLater();

        emit selectionFailed(tr("无法抓取屏幕内容。请确认当前系统允许截图，"
                                "并确认应用运行在图形桌面会话中。"));
    }
}

void PluginManager::restoreSourceWidget()
{
    if (!m_sourceWidget)
        return;

    m_sourceWidget->show();
    m_sourceWidget->raise();
}
