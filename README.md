# GameEngine

从 GameDev 教学项目抽离出来的 2D 游戏引擎层：场景管理、实体帧动画、TTF 中文字体渲染、INI 配置解析与音效播放。

## 简介

本仓库是「追币」教学游戏（GameDev）的引擎层，经 git 历史抽取独立成库
（`git filter-repo`：`Src/inc/engine/`、`Src/cpp/engine/`、`Src/inc/Rect.h` → `include/engine/`、`src/`），
引入以来的完整提交历史随仓保留，可用 `git log` 追溯每处设计决策的来龙去脉。

职责边界：

- **引擎层（本仓库）**：与具体玩法无关的框架能力——场景生命周期、固定时间步长更新、
  实体与帧动画、字体渲染、调试文字、配置解析、音效播放。
- **游戏层（宿主仓库）**：玩法规则、场景内容与资源（如 PlayScene、Player/Coin 等）。

## 目录结构

```
GameEngine/
├─ CMakeLists.txt       # 静态库构建脚本（target：GameEngine）
├─ include/engine/      # 公开头文件（9 个）
│  ├─ UpdateContext.h   #   固定时间步长下的更新上下文（delta / 插值系数）
│  ├─ SceneManager.h    #   场景管理器（延迟切换、帧末结算）
│  ├─ Scene.h           #   场景基类（事件 / 更新 / 渲染回调）
│  ├─ Rect.h            #   矩形与碰撞检测工具
│  ├─ Font.h            #   TTF 字体加载与文本渲染
│  ├─ Entity.h          #   实体基类（纹理、位置、帧动画）
│  ├─ DebugText.h       #   调试文字叠加显示
│  ├─ Config.h          #   INI 配置解析器
│  └─ AudioSystem.h     #   音效系统
└─ src/                 # 实现文件（5 个）
   ├─ SceneManager.cpp
   ├─ Font.cpp
   ├─ Entity.cpp
   ├─ Config.cpp
   └─ AudioSystem.cpp
```

## 依赖说明

引擎**不内嵌** SDL 源码，构建时使用宿主工程已提供的四个 SDL 组件目标，避免同一 SDL 被重复构建：

| CMake 目标 | 用途 | 链接方式 |
| --- | --- | --- |
| `SDL3::SDL3` | 窗口 / 渲染 / 事件基础 | PUBLIC —— 随引擎头文件传递给宿主 |
| `SDL3_image::SDL3_image` | PNG 纹理加载 | PRIVATE —— 仅引擎内部实现 |
| `SDL3_ttf::SDL3_ttf` | TTF 中文字体渲染 | PRIVATE —— 仅引擎内部实现 |
| `SDL3_mixer::SDL3_mixer` | WAV 音效播放 | PRIVATE —— 仅引擎内部实现 |

宿主未提供 `SDL3::SDL3` 时，配置阶段会 `FATAL_ERROR` 并提示「宿主工程需先提供四个 SDL 组件」。

## 接入方式

在宿主工程的 CMakeLists.txt 中先引入四个 SDL 组件，再挂入本引擎：

```cmake
# 1. 宿主先提供 SDL 四件套（此处以 add_subdirectory 形式为例）
add_subdirectory(vendor/SDL-main EXCLUDE_FROM_ALL)
add_subdirectory(vendor/SDL3_image EXCLUDE_FROM_ALL)
add_subdirectory(vendor/SDL3_ttf EXCLUDE_FROM_ALL)
add_subdirectory(vendor/SDL3_mixer EXCLUDE_FROM_ALL)

# 2. 以子目录形式接入引擎
add_subdirectory(GameEngine)

# 3. 宿主程序链接引擎静态库（PUBLIC 的 SDL3::SDL3 会随之自动传递）
target_link_libraries(GameDev PRIVATE GameEngine)
```

## include 约定

引擎头文件经 `target_include_directories(... PUBLIC include)` 挂载，
宿主与引擎内部统一写 `"engine/xxx.h"`，不出现随文件深度变化的相对路径：

```cpp
#include "engine/SceneManager.h" // 场景管理器
#include "engine/Entity.h"       // 实体基类
#include "engine/Rect.h"         // 矩形与碰撞工具
```

## 来源说明

- 抽取自 GameDev 仓库（原路径 `Src/inc/engine/`、`Src/cpp/engine/`、`Src/inc/Rect.h`），提交历史可查。
- `Rect.h` 早期位于 `Src/inc/Rect.h`，后随「目录分层」重构迁入 `Src/inc/engine/`；
  抽取后的历史中统一显示为 `include/engine/Rect.h`，内容与最初引入时逐字节一致。
- 本仓库为纯本地库，不内置 SDL、不含构建产物（见 `.gitignore`）。
