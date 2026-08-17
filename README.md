# BaoXiaoXin Writer

轻量、原生 Windows 的剪贴板键盘输入工具，专为代码编辑器和不方便直接粘贴的输入场景设计。

![BaoXiaoXin Writer hero](assets/hero.png)

BaoXiaoXin Writer 从剪贴板或文本面板读取内容，通过 Windows `SendInput` 逐字符发送到当前焦点窗口。它支持完整 Unicode 输入、可调速输入、全局热键、题库搜索，以及面向 Python 在线代码编辑器的自动缩进补偿。

> **首次运行请注意：** 双击 `KeyboardSim.exe` 后，程序默认进入系统托盘并在后台运行，不会自动弹出主窗口。这是正常的初始状态，不是启动失败。在任务栏右下角的通知区域找到程序图标，**左键单击**即可打开主窗口。

> 项目定位是本地输入辅助和开发工具。请遵守目标网站的使用规则、考试规定和服务条款。

## 主要功能

- **剪贴板键盘输入**：将文本逐字符输入到当前焦点窗口，支持中英文和 Unicode。
- **Python 在线编辑器模式**：忽略原文行首空格，交给网站编辑器自动缩进，并对 `if/else` 等代码块做回退补偿。
- **可调输入速度**：支持慢速、中速、快速预设，也可以手动设置 10～500ms 字符间隔。
- **文件加载**：从文本文件加载待输入内容。
- **全局热键**：窗口隐藏到托盘后仍可启动、搜索和停止输入。
- **内置题库**：使用静态编译的 SQLite，支持题目与答案管理、模糊搜索，不需要额外的 `sqlite3.dll`。
- **浅色界面**：默认使用浅灰/白色背景，适合长时间使用。
- **响应式输入线程**：输入任务运行在独立 worker 线程中，主界面保持响应。

## 界面预览

主界面用于加载文本、调整输入速度、开始或停止输入，以及进入题库管理。

![BaoXiaoXin Writer 主界面](screenshot1.png)

题库管理界面支持批量导入、单条添加、删除和导出题目。

![BaoXiaoXin Writer 题库管理界面](screenshot2.png)

## 工作流程

![Clipboard to editor workflow](assets/workflow.png)

程序不会把代码原有的行首空格再次机械地叠加到网站自动缩进上。开启 Python 模式后，原始缩进只用于分析目标层级，实际的换行和基础缩进交给目标编辑器处理。

## 快速开始

### 直接运行

从 Releases 下载 `KeyboardSim.exe`，双击运行即可，无需安装 Python 或 SQLite DLL。

1. 双击运行 `KeyboardSim.exe`。程序会进入系统托盘，主窗口不会自动显示。
2. 如需调整速度、输入模式或题库，左键单击托盘图标打开主窗口（右键菜单也可以）。
3. 复制要输入的文本。
4. 将光标放到目标输入框。
5. 按 `Ctrl+Alt+V` 开始输入。
6. 按 `Ctrl+Alt+S` 立即停止。

如果托盘图标没有直接显示，请先点击任务栏通知区域的向上箭头，在隐藏图标中查找。

**关闭与退出的区别：** 主窗口的关闭按钮只是把窗口收回托盘，程序继续在后台运行、热键继续有效（第一次收回时会有气泡提示）。需要**完全退出**请右键托盘图标选择“退出”。

程序是单实例的：重复双击不会启动第二个进程，而是把已经运行的那个主窗口显示出来。

### Python 在线编辑器模式

在主面板勾选：

```text
在线网站 Python 补偿（含行首缩进过滤）
```

这个模式适合目标网站已经会自动缩进的 Python 编辑器。它会：

- 忽略剪贴板中每行开头的空格和 Tab；
- 根据 Python 的 `def`、`if`、`else`、`for`、`while`、`try` 等代码块判断层级；
- 对网站多出来的缩进发送对应次数的 Backspace；
- 跳过只包含空格的空白行，避免空白行重复触发补偿；
- 保留注释内容，不把注释当作代码块结束标志。

不同网站的编辑器实现可能不同。建议先使用短代码样例验证，再输入较长程序。

## 全局热键

| 热键 | 功能 |
| --- | --- |
| `Ctrl+Alt+V` | 开始输入剪贴板或面板内容 |
| `Ctrl+Alt+B` | 用剪贴板中的题目搜索内置题库 |
| `Ctrl+Alt+S` | 停止当前输入 |

**热键被占用时会明确提示。** 这些组合可能被截图工具、输入法或录屏软件抢占。如果启动时有热键注册失败，程序会弹出托盘气泡，点名具体是哪一个，主窗口的提示区域也会标注“（被占用）”。关闭占用程序后，右键托盘图标选择“重试注册热键”即可，不需要重启。热键失败不影响从主窗口手动操作。

## 题库功能

复制题目后按 `Ctrl+Alt+B`：

- 主窗口可见时，匹配到的答案会写入文本面板；
- 主窗口隐藏时，匹配到的答案会直接写入剪贴板；
- 可在数据库管理窗口中添加、导入、删除题目和答案；
- 支持模糊搜索。

SQLite 已经静态编译进程序，运行时不依赖外部 `sqlite3.dll`。

## 从源码构建

### 环境

- Windows
- MinGW GCC
- GNU Make

### 构建

```bat
git clone https://github.com/la-olla-de-cobre/baoxiaoxin-writer.git
cd baoxiaoxin-writer
mingw32-make TARGET=KeyboardSim.exe
```

输出文件为：

```text
KeyboardSim.exe
```

GitHub Actions 会在 Windows runner 上使用 MSYS2/MinGW 自动执行构建检查。

## 项目结构

```text
src/
├─ main.c        窗口、热键和应用生命周期
├─ ui.c          Win32 界面与浅色主题
├─ worker.c      键盘输入、缩进补偿和 worker 线程
├─ database.c    SQLite 数据库封装
├─ qa_ui.c       题库管理界面
├─ config.c      配置加载与保存
├─ mem.c         统一堆分配接口（Mem_*）
└─ textfile.c    文本文件读取与编码探测
third_party/
└─ sqlite/       SQLite amalgamation
res/              Windows 资源、manifest、应用图标与版本信息
assets/           README 项目图片
scripts/          资源生成脚本（make_icon.py 生成 res/app.ico）
tests/            回归测试（mingw32-make test）
```

版本号、窗口标题与 exe 版本元数据统一来自 `src/resource.h` 的 `APP_VER_*`
宏，发版时只改这一处。应用图标由 `scripts/make_icon.py` 生成多尺寸
`.ico`（16/20/24/32/48/64/128/256），需要改图标时改脚本后重新运行。

### 内存管理约定

全项目只通过 `src/mem.h` 的 `Mem_Alloc` / `Mem_AllocZero` / `Mem_Realloc` /
`Mem_WcsDup` / `Mem_Free` 分配和释放内存，不再混用 `HeapAlloc` 与 `malloc`。
凡是返回堆指针的接口都在声明处注明由调用方 `Mem_Free`。

## 技术栈

| 模块 | 实现 |
| --- | --- |
| GUI | Win32 API、Common Controls |
| 键盘输入 | `SendInput`、`KEYEVENTF_UNICODE` |
| Unicode | Windows wide-character API |
| 线程 | `_beginthreadex`、Windows Events |
| 数据库 | 静态 SQLite amalgamation |
| 构建 | MinGW GCC、GNU Make |
| 自动检查 | GitHub Actions + MSYS2 |

## 配置

用户配置由程序运行时保存，包含输入速度、窗口置顶、Python 输入模式和最近使用路径等设置。配置不会写入可执行文件本身。

## 许可证

MIT License
