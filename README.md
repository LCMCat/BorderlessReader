# BorderlessReader（阅界）


**阅界（BorderlessReader）** 是外文论文阅读辅助系统的**最小实现**。本仓库只实现公共基础设施和示例插件，不包含翻译、查词、公式识别等业务逻辑。

技术栈：**C++17 + Qt 6 Widgets（不使用 QML）+ CMake**。优先支持 Windows，代码没有直接调用 Win32 API，尽量保持跨平台。

---

## 1. 已实现的能力

- `FloatBar`：无边框、始终置顶、纵向工具卡片、拖动、屏幕边缘吸附、右键菜单（回到右上角 / 退出）。
- `RegionSelector`：抓取整个虚拟桌面、全屏遮罩、灰度暗化、十字光标、实时选区、尺寸提示、Esc / 右键取消。
- `IToolPlugin`：极简插件接口；插件在编译期注册，不依赖 `QPluginLoader`。
- `PluginManager`：持有插件；协调“隐藏悬浮条 → 框选 → 恢复悬浮条 → 回调插件”的完整链路。
- `ScreenUtils` / `CaptureUtils`：全局坐标、屏幕安全回退、虚拟桌面包围盒、HiDPI 像素换算、按全局 `QRect` 裁剪。
- `DemoPlugin`：显示全局坐标、逻辑尺寸、截图像素、DPR 和缩略图，用于验证框架链路。

---

## 2. 构建与运行

### 环境要求

- Windows 10/11（当前验证环境：Windows 11 26200）
- Qt **6.5+**，验证版本 **Qt 6.8.3 MSVC 2022 64-bit**
- CMake 3.21+（验证版本 4.3.3）
- MSVC 2022 或与 Qt 匹配的 MinGW 工具链
- 支持 C++17

### Windows + MSVC 命令行

在 PowerShell 中执行即可；Visual Studio 生成器会自行找到已安装的 MSVC：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64

cmake --build build --config Release --parallel

# 让本次终端能找到 Qt DLL 和平台插件
$env:PATH = "C:\Qt\6.8.3\msvc2022_64\bin;$env:PATH"
.\build\Release\BorderlessReader.exe
```

如果你使用 MinGW 版 Qt，请换用与 Qt 完全匹配的 MinGW 编译器和 CMake 生成器，例如 `-G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\mingw_64`。

如果使用 Qt Creator：直接打开 `CMakeLists.txt`，选择已安装的 Qt 6 Kit，构建并运行 `BorderlessReader`。

### 打包为可独立运行的目录

在可用的 Qt 命令行环境中，对构建产物执行：

```powershell
windeployqt --release .\build\Release\BorderlessReader.exe
```

然后分发 `build\Release\` 目录。注意不要只复制 exe，Qt 的平台插件（如 `platforms/qwindows.dll`）也必须一并存在。

### 操作方式

| 操作 | 结果 |
| --- | --- |
| 按住工具卡 / 顶部拖拽把手拖动 | 移动悬浮条 |
| 松手时靠近屏幕边缘 | 自动吸附到该屏幕的工作区边缘 |
| 单击工具卡 | 启动全屏框选 |
| 在框选区拖拽后松开 | 生成 `QRect + QPixmap`，回调插件 |
| 框选时按 `Esc` / 鼠标右键 | 取消，恢复悬浮条 |
| 右键悬浮条 | “吸附到当前屏幕右上角” / 退出 |
| 结果窗口按 `Esc` | 关闭 Demo 窗口 |


## 3. 新增自己的工具插件

### 第 1 步：实现接口

```cpp
// plugins/MyTranslatePlugin.h
#pragma once

#include "core/IToolPlugin.h"

class MyTranslatePlugin final : public IToolPlugin
{
public:
    QString name() const override { return QStringLiteral("我的翻译"); }
    QIcon icon() const override;   // 没有资源文件时可以 QPainter 画一个
    void onRegionSelected(const QRect &globalRect,
                          const QPixmap &regionPixmap) override;
};
```

```cpp
// plugins/MyTranslatePlugin.cpp
#include "plugins/MyTranslatePlugin.h"

#include <QApplication>
#include <QMessageBox>
#include <QStyle>

QIcon MyTranslatePlugin::icon() const
{
    // 示例：先借用一个系统标准图标。正式插件可换成自己的 QIcon / 资源。
    return QApplication::style()->standardIcon(QStyle::SP_FileDialogDetailedView);
}

void MyTranslatePlugin::onRegionSelected(const QRect &globalRect,
                                         const QPixmap &regionPixmap)
{
    // IToolPlugin 不是 QObject，不能直接调用 tr()，这里用 QStringLiteral。
    // 真正业务由你接入；globalRect 是全局逻辑坐标，regionPixmap 是物理像素截图。
    qInfo() << globalRect << regionPixmap.size() << regionPixmap.devicePixelRatio();
    QMessageBox::information(
        nullptr,
        QStringLiteral("选区"),
        QStringLiteral("已拿到 %1 x %2 的选区")
            .arg(globalRect.width())
            .arg(globalRect.height()));
}
```

插件回调运行在 GUI 线程。若把截图交给工作线程，请复制 `QPixmap`（或转成 `QImage`）后再跨线程发送，`QPixmap` 本身不保证线程安全。

### 第 2 步：加入 CMake

```cmake
qt_add_executable(BorderlessReader WIN32
    # ...
    plugins/MyTranslatePlugin.h
    plugins/MyTranslatePlugin.cpp
)
```

### 第 3 步：在 main.cpp 注册

```cpp
pluginManager.addPlugin(std::make_unique<DemoPlugin>());
pluginManager.addPlugin(std::make_unique<MyTranslatePlugin>());  // 新增
```

注册顺序对应悬浮条中从上到下的顺序。工具卡会自动循环使用奶油白、深海军蓝、雾蓝、薄荷绿、香芋紫五种参考图配色，不需要插件关心样式。

### 禁忌

- 不要在插件里再调用一次选区器；
- 不要把 `globalRect` 当成窗口局部坐标；
- 不要在插件里 `delete` 自己，生命周期由 `PluginManager` 管理；
- 不要为了避免“空选区”而忽略 `globalRect`，框架已经过滤了小于 4×4 DIP 的误点击。
