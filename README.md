# BorderlessReader（阅界）


**阅界（BorderlessReader）** 一款外文论文阅读辅助系统。本仓库包含公共基础设施和选区演示和查词功能。

技术栈：**C++17 + Qt 6 Widgets（不使用 QML）+ CMake**。优先支持 Windows，代码没有直接调用 Win32 API，尽量保持跨平台。

---

## 1. 已实现的能力

- `FloatBar`：无边框、始终置顶、纵向工具卡片、拖动、屏幕边缘吸附、右键菜单（回到右上角 / 退出）。
- `RegionSelector`：抓取整个虚拟桌面、全屏遮罩、灰度暗化、十字光标、实时选区、尺寸提示、Esc / 右键取消。
- `IToolPlugin`：极简插件接口；插件在编译期注册，不依赖 `QPluginLoader`。
- `PluginManager`：持有插件；协调“隐藏悬浮条 → 框选 → 恢复悬浮条 → 回调插件”的完整链路。
- `ScreenUtils` / `CaptureUtils`：全局坐标、屏幕安全回退、虚拟桌面包围盒、HiDPI 像素换算、按全局 `QRect` 裁剪。
- `Selection`：框选回调除选区截图外，还带**整屏快照**；插件需要上下文时不必自己重新抓屏。
- `core/Ocr`：选区截图 → 文字 + 词框。Windows 上用系统自带的 `Windows.Media.Ocr`，工作线程串行识别；没有后端时自动降级并给出原因，不影响其它功能。
- `DemoPlugin`：显示全局坐标、逻辑尺寸、截图像素、DPR 和缩略图，并调用 `core/Ocr` 把选区识别出的文字与词框一并显示出来，用于验证框架链路。
- `WordLookupPlugin`：即时查词与一词多义。本地 ECDICT 全量词库、数据驱动词形还原、浮窗 UI（Credit：邹同学）。

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

### OCR

OCR 后端按优先级自动选择，只在 **Windows + MSVC** 下编译：

1. **Tesseract 5**（首选）：运行库随仓库放在 `third_party/tesseract/`，运行时用 `LoadLibrary` 动态加载，编译期零依赖。
2. **Windows.Media.Ocr**（兜底）：Tesseract 运行库缺失时自动退回系统 OCR。
3. 都没有：`core/Ocr` 报告不可用，插件照常可用（手动输入）。

运行库目录结构（`third_party/tesseract/`，约 82 MB）：

```
bin/tesseract55.dll        + 依赖 DLL
tessdata/eng.traineddata
```

构建后 CMake 会把它复制到 exe 同级；运行时路径固定为 `<exe>/third_party/tesseract`，没有环境变量和父目录搜索。**报“加载 tesseract55.dll 失败（错误码 126）”就是运行库没在 exe 旁边**——从 `build/Release` 直接运行，或按“打包与分发”把 `third_party` 一起复制。

```powershell
# 只关掉 Tesseract（退回 Windows OCR）
cmake -S . -B build -DPAPERREADER_ENABLE_TESSERACT=OFF
# 把两个后端都关掉
cmake -S . -B build -DPAPERREADER_ENABLE_TESSERACT=OFF -DPAPERREADER_ENABLE_WINOCR=OFF
```

配置阶段会打印实际生效的后端，例如：

```
-- BorderlessReader: OCR backend = Tesseract (runtime: third_party/tesseract)
-- BorderlessReader: OCR fallback = Windows.Media.Ocr
```

### 词库数据

查词用的 ECDICT 全量词库随仓库提交在 `dict/`：

```
dict/ecdict_full.tsv   77 万词条
dict/forms_full.tsv    词形变化表
```

### 打包与分发

Qt 默认动态链接：`BorderlessReader.exe` 运行时需要同目录下的 Qt 库与平台插件。目标机器无需安装 Qt，但下列文件必须随 exe 一起分发。

```powershell
# 1. 构建
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build --config Release --parallel

# 2. 部署 Qt 运行库
$dist = "$PWD\dist\BorderlessReader"
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item .\build\Release\BorderlessReader.exe $dist

& C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe --release --no-translations `
    --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime `
    --dir $dist "$dist\BorderlessReader.exe"

# 3. 部署数据与 OCR 运行库（缺了会查不到词 / 报错误码 126）
New-Item -ItemType Directory -Force "$dist\dict", "$dist\third_party" | Out-Null
Copy-Item .\dict\*.tsv "$dist\dict"
Copy-Item .\third_party\tesseract "$dist\third_party\tesseract" -Recurse

# 4. 部署 MSVC 运行库（目标机器未安装 VC++ Redistributable 时需要）
$crt = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
Copy-Item "$crt\*.dll" $dist
```

第 4 步的路径与版本号以本机 Visual Studio 安装为准。

发布 `dist\BorderlessReader\` 整个目录，结构不可拆散：

```
BorderlessReader.exe
Qt6Core.dll  Qt6Gui.dll  Qt6Widgets.dll
msvcp140.dll  vcruntime140.dll  vcruntime140_1.dll
platforms\qwindows.dll      # 缺失时报 could not find or load the Qt platform plugin "windows"
styles\  imageformats\  ...
dict\ecdict_full.tsv  forms_full.tsv
third_party\tesseract\bin\tesseract55.dll  tessdata\eng.traineddata
```

验证：在未安装 Qt 的机器上（或临时把 Qt 从 `PATH` 移除后）直接运行 exe，悬浮条正常出现即依赖完整；框一个词能出释义、且 `third_party` 缺省时退回 Windows OCR，说明两套依赖都部署对了。

#### 单文件 exe

`windeployqt` 产出的是目录。确需单个 exe 时二选一：

- **静态链接 Qt**：从源码以 `-static -static-runtime` 构建 Qt，再用该 Qt 重新构建本项目；产物约 15–25 MB，不依赖任何 Qt DLL。LGPLv3 下静态链接需提供可重新链接的目标文件，否则应使用商业授权。

  ```powershell
  # 影子构建，源码位于 C:\Qt\src\6.8.3
  New-Item -ItemType Directory C:\Qt\build-static -Force | Out-Null
  Set-Location C:\Qt\build-static
  C:\Qt\src\6.8.3\configure.bat -release -static -static-runtime -opensource `
    -confirm-license -nomake examples -nomake tests -prefix C:\Qt\6.8.3-static
  cmake --build . --parallel
  cmake --install .

  # 用静态 Qt 重新构建本项目
  cmake -S . -B build-static -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3-static
  cmake --build build-static --config Release --parallel
  ```

- **打包壳**：用 Enigma Virtual Box 等工具把 `dist\BorderlessReader\` 封装为单个 exe，无需重编 Qt；运行时释放到临时目录，启动略慢，并可能被杀毒软件误报。

### 操作方式

| 操作 | 结果 |
| --- | --- |
| 按住工具卡 / 顶部拖拽把手拖动 | 移动悬浮条 |
| 松手时靠近屏幕边缘 | 自动吸附到该屏幕的工作区边缘 |
| 单击工具卡 | 启动全屏框选 |
| 在框选区拖拽后松开 | 生成 `Selection`（选区 + 整屏快照），回调插件 |
| 框选完成（「选区演示」）| 结果窗口先出现，OCR 文字与词框随后异步填充 |
| 框选完成（「查词」）| 浮窗出现在选区右侧，显示音标、按词性分组的中文义项与词形 |
| 框选时按 `Esc` / 鼠标右键 | 取消，恢复悬浮条 |
| 右键悬浮条 | “吸附到当前屏幕右上角” / 退出 |
| 结果窗口按 `Esc` | 关闭结果窗口 |


## 3. OCR 能力（core/Ocr）

```cpp
#include "core/Ocr/TextRecognizer.h"

Ocr::Recognizer recognizer;   // 插件成员；自己持有自己的工作线程

// 回调保证在 GUI 线程执行；image 按值传入，调用方可以立刻释放 QPixmap。
recognizer.recognize(regionPixmap.toImage(), [this](const Ocr::Result &result) {
    if (!result.isOk()) {
        qWarning() << result.error;
        return;
    }
    for (const Ocr::Word &word : result.words)
        qInfo() << word.text << word.box;   // box 是图片像素坐标
});
```

约定：

- **线程**：公开方法只在 GUI 线程调用；只起一条工作线程，请求串行处理，不会每次识别都新建线程。
- **生命周期**：`Recognizer` 通常由插件持有。析构会等工作线程结束，并丢弃尚未派发的回调——回调里捕获插件的 `this` 是安全的。
- **能力查询**：`probe()` 异步初始化后端；结果通过 `stateChanged()` 通知，`state()` / `backendDescription()` / `unavailableReason()` 随时可读。
- **取消**：`recognize()` 返回请求号，`cancel()` 可丢弃排队中的请求；已经开始的那次识别无法中途打断。
- **坐标**：`Word::box` 是像素坐标，不是 DIP；需要逻辑坐标时按 `regionPixmap.devicePixelRatio()` 自己换算。

---

## 4. 即时查词（plugins/WordLookup）

浮窗 UI（无边框、配色、音标、词性分组、历史、复制、拖拽、失焦收起）原样保留贡献者版本；**逻辑按机制/策略重写**：

- **取词**：`core/Ocr` 识别选区周围一整块上下文，`WordPicker` 再用词框挑出落入选区的词。选区内没有词就直接不猜。
- **查词**：`LookupService` 按顺序询问 provider，返回第一个命中；现在只有本地 ECDICT。`未收录` 与 `词库不可用` 是两种结局，分别提示。
- **词形**：只查 `dict/forms_full.tsv`，不做规则推导、不手写不规则表。
- **模糊纠错**：默认关闭；`MatchKind::Fuzzy` 只作为以后的可选策略预留。
- **在线词典**：接口 `LookupProvider` 已留好，以后实现一个 provider 插进 `LookupService` 即可，UI 不用改。

```
plugins/WordLookup/
  WordLookupPlugin.*   注册与编排（OCR → 选词 → 查询 → 浮窗）
  LookupDialog.*       原版浮窗 UI，只通过回调拿数据
  WordPicker.*         选词策略
  lookup/              LookupService / LookupProvider / LocalDictionary / WordForms / LookupResult
```

---

## 5. 新增自己的工具插件

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
    void onSelection(const Selection &selection) override;   // 选区 + 整屏快照
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

void MyTranslatePlugin::onSelection(const Selection &selection)
{
    // IToolPlugin 不是 QObject，不能直接调用 tr()，这里用 QStringLiteral。
    // 真正业务由你接入；selection.globalRect 是全局逻辑坐标，
    // selection.regionPixmap 是选区物理像素截图，selection.desktopImage 是整屏快照。
    const QRect &globalRect = selection.globalRect;
    const QPixmap &regionPixmap = selection.regionPixmap;
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
- 不要为了避免“空选区”而忽略 `globalRect`，框架已经过滤了小于 4×4 DIP 的误点击；
- 不要自己起线程接 WinRT / Tesseract 做 OCR——用 `core/Ocr`（见第 3 节），引擎和线程是框架的事；
- 不要在插件里重新抓屏——框架已经把选区像素给你了；确实需要整屏快照时，应该先扩展框架接口，而不是让插件绕过去。
