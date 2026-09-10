# 代码行数统计 (Code Line Counter)

一个 C++17 实现的代码行数统计工具，提供**命令行**和**网页**两种使用方式。统计每个语言的 `files`（文件数）、`blank`（空行）、`comment`（注释行）、`code`（代码行）。

## 功能特性

- **命令行工具** `cloc.exe`：统计指定目录（默认当前目录），输出对齐的表格。
- **网页服务** `cloc-server.exe`：本地启动网页，可视化统计。
  - 输入目录路径或通过「浏览…」弹窗选择文件夹
  - 支持**拖拽文件夹**到页面，自动上传并统计
  - 支持同时统计**多个目录**，结果自动合并汇总
  - 自适应明暗主题（跟随系统 `color-scheme`）
- **准确的计数逻辑**：区分空行、行注释、块注释、字符串字面量（字符串内的 `//`、`#` 不会被误判为注释），跳过二进制文件，自动处理 CRLF 与 UTF-8 BOM。
- **Windows 中文路径友好**：UTF-8 与系统宽字符路径（UTF-16/GBK）在边界处正确转换，避免中文目录乱码。

## 支持的语言

| 语言   | 扩展名                              | 注释语法                    |
| ------ | ----------------------------------- | --------------------------- |
| C      | `.c` `.h`                           | `//` `/* */`                |
| C++    | `.cpp` `.cc` `.cxx` `.hpp` `.hh` `.hxx` | `//` `/* */`            |
| Java   | `.java`                             | `//` `/* */`                |
| Python | `.py`                               | `#` `"""` `'''`             |

> 如需增加更多语言，在 [counter.hpp](counter.hpp) 的 `languages()` 表中加一行即可。

## 构建

需要 Windows + [Visual Studio 2022](https://visualstudio.microsoft.com/)（含 MSVC C++ 工具链）。

双击运行 [build.bat](build.bat)，或在「开发者命令提示符」中执行：

```bat
cl /nologo /EHsc /std:c++17 /utf-8 /Fe:cloc.exe main.cpp
cl /nologo /EHsc /std:c++17 /utf-8 /Fe:cloc-server.exe server.cpp /link Ws2_32.lib
```

生成两个可执行文件：

- `cloc.exe` —— 命令行版本
- `cloc-server.exe` —— 网页版本（需要 `Ws2_32.lib`）

## 使用

### 命令行

```bat
cloc.exe                  :: 统计当前目录
cloc.exe D:\MyProject     :: 统计指定目录
```

示例输出：

```
------------------------------------------------------------
Language       files     blank    comment       code
------------------------------------------------------------
C                  3        50         20        320
C++               10       180         75       1400
Java               2        30         12        180
Python             5        90         40        520
------------------------------------------------------------
SUM               20       350        147       2420
------------------------------------------------------------
```

### 网页

```bat
cloc-server.exe            :: 默认端口 8080
cloc-server.exe 9000       :: 指定端口 9000
```

启动后浏览器打开 <http://localhost:8080>，即可在页面上添加目录、拖拽文件夹或浏览选择目录进行统计。

## HTTP API（网页服务）

| 方法 | 路径                        | 说明                                              |
| ---- | --------------------------- | ------------------------------------------------- |
| GET  | `/`                         | 返回网页 UI                                      |
| GET  | `/api/browse?path=...`      | 浏览目录（`path` 为空时列出磁盘盘符）            |
| GET  | `/api/count?dir=a&dir=b`    | 统计一个或多个目录，返回 JSON                     |
| POST | `/api/count-files`          | 统计上传的文件（`multipart/form-data`，字段名 `files`） |

`/api/count` 返回示例：

```json
{
  "ok": true,
  "error": "",
  "languages": [
    { "name": "C", "files": 3, "blank": 50, "comment": 20, "code": 320 }
  ],
  "total": { "files": 3, "blank": 50, "comment": 20, "code": 320 }
}
```

## 项目结构

```
code-line-counter/
├── main.cpp        # 命令行入口
├── server.cpp      # 网页服务（HTTP 路由 + 内嵌 HTML UI）
├── counter.hpp     # 核心统计逻辑（header-only，CLI 与 server 共用）
├── httplib.h       # 第三方库 cpp-httplib（单头文件）
└── build.bat       # MSVC 构建脚本
```

## 依赖

- [cpp-httplib](https://github.com/yhirose/cpp-httplib)（已内置于 [httplib.h](httplib.h)）
- 其余仅使用 C++17 标准库


