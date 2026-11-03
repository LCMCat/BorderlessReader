#include "plugins/DemoPlugin.h"
#include "plugins/WordLookup/WordLookupPlugin.h"
#include "shell/FloatBar.h"
#include "shell/PluginManager.h"

#include <QApplication>
#include <QFont>
#include <QGuiApplication>
#include <QStyleFactory>

#include <memory>

int main(int argc, char *argv[])
{
    // Qt 6 默认已经开启高 DPI 支持。PassThrough 保留 1.25/1.5 等非整数缩放，
    // 避免 Windows 上被四舍五入后出现尺寸跳变。
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BorderlessReader"));
    app.setApplicationDisplayName(QStringLiteral("阅界"));
    app.setOrganizationName(QStringLiteral("BorderlessReader"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // 统一 Fusion 风格，配合自定义绘制，让不同平台/主题下的基础控件一致。
    if (QStyleFactory::keys().contains(QStringLiteral("Fusion")))
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont appFont = app.font();
    appFont.setFamilies({
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("Segoe UI"),
        QStringLiteral("sans-serif"),
    });
    appFont.setPointSizeF(9.5);
    app.setFont(appFont);

    // 框选期间悬浮条会被隐藏。如果不关闭“最后一个窗口退出”，应用可能在
    // 选区器关闭的瞬间被退出。退出动作由悬浮条右键菜单显式触发。
    QApplication::setQuitOnLastWindowClosed(false);

    PluginManager pluginManager;

    // 这里是本 MVP 的“编译期插件注册”入口。新增插件时：
    //   1. 实现 IToolPlugin；
    //   2. 在 CMakeLists.txt 添加源文件；
    //   3. 在下面增加一行 addPlugin()，顺序即工具栏中的显示顺序。
    pluginManager.addPlugin(std::make_unique<DemoPlugin>());
    pluginManager.addPlugin(std::make_unique<WordLookupPlugin>());

    FloatBar floatBar(&pluginManager);
    floatBar.show();
    floatBar.raise();

    return app.exec();
}
