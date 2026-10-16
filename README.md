# GameEngine

从 **COIN RUSH（金币冲刺）** 游戏中沉淀出来的轻量 2D 游戏引擎：构建在 SDL3 之上、以 C++20 编写，提供与具体玩法无关的框架能力——场景管理、实体与帧动画、文本渲染、音效播放、配置解析与碰撞判定。

引擎不内嵌 SDL，也不包含主循环与玩法规则（这两者属于宿主）；本仓库是一个纯静态库工程。

## 特性一览

### Entity（实体基类）

- 统一「位置 / 尺寸 + 纹理生命周期 + 精灵表帧动画」的公共能力；子类只需在构造时固定尺寸、在 `load()` 里提供素材路径与回退色。
- 帧动画按固定步长的逻辑时间轴推进（与渲染帧率解耦）；帧数为 1 时帧号恒为 0，单帧图 / 回退色块自动退化为静止画面。
- 三级资源回退（精灵表 → 单帧图 → 程序化纯色）；插值快照纪律：`update` 首行调用 `snapshotPreviousPosition()`，`render(alpha)` 在上一固定步位置与当前位置之间线性插值。
- `setFlipped(bool)`：水平翻转绘制（SDL 翻转参数）。纯渲染层标记——碰撞、插值、判定等逻辑一律不受镜像影响。
- `setAnimationRange(first, last)`：动画循环子区间（默认全区间，`first == last` 定格该帧）。典型用法：跑动帧与站立帧拼进同一张精灵表，按状态切换子区间。
- `setCollisionBox(w, h, offsetX, offsetY)` / `collisionRect()` / `collisionRectAt(x, y)`：碰撞盒——把「判定体积」与精灵视觉解耦（缺省 = 整张精灵、偏移 0，未设置时与 `rect()` 完全一致）。物理用途（墙碰撞滑动、落点校验、实体互碰、拾取）取 `collisionRect()`（逻辑位置、固定步内确定）、渲染/合成照旧用完整精灵；典型用法是俯视角「脚部小盒」——身体可自然叠压墙脚而脚盒不穿透。
- 纹理统一以最近邻采样（`SDL_SCALEMODE_NEAREST`）：像素风素材整数倍放大时保持像素锐利，不被线性插值糊化（三条回退路径一致）。

### Scene 与 SceneManager（场景框架）

- `Scene` 基类：`onEnter` / `onExit` / `handleEvent` / `update` / `render` 五个回调，全部带默认空实现，无逻辑场景（如菜单）可以零覆写。
- 延迟切换：`requestScene()` 只登记请求，帧末 `applyPendingChange()` 统一结算（旧场景 onExit → 析构 → 新场景 onEnter），避免在场景自身回调执行期间把 `this` 销毁。
- 依赖注入：`onEnter` 一次性收到 `SDL_Renderer*`、`const Config&`、`AudioSystem&`，分别用于加载资源、读取可调数值、触发音效；所有权都在宿主，场景只借用。

### UpdateContext（固定步长世界上下文）

- 把一次 `update` 所需的稳定环境打包成只读结构按值传递：固定步长 `deltaTime`、键盘状态（过渡期保留、迁移完成后移除）、视口尺寸、输入系统指针。
- 固定步长主循环**不属于引擎**（由宿主实现）；引擎只提供这条「每步输入」契约——将来扩展字段（鼠标位置等）不会改动实体一方的调用形式。

### Input（动作映射与边沿检测：键盘 + 鼠标）

- 把物理按键映射为语义动作：`bind("move_up", SDL_SCANCODE_W)`，重复绑定即改键；`held` / `pressed` / `released` 三种查询，未绑定动作一律安全返回 false。
- 鼠标同库支持：`bindMouse("attack", SDL_BUTTON_LEFT)` 把动作映射到鼠标按钮（左 / 中 / 右键等），`mousePosition()` 返回采样时的指针位置（`Vec2`、窗口客户区坐标）。同一动作可同时拥有键盘与鼠标绑定，查询时两路状态合并（任一路按下即动作按下）——玩法只问动作名，不关心输入设备。
- 按渲染帧采样一次（`update`）：一次调用同时取键盘数组与鼠标状态（按钮掩码 + 指针位置），两类输入共享同一份帧快照，边沿在整帧内对所有固定步可见；`updateFromKeyArray` / `updateFromMouseState` 供单元测试注入合成状态，边沿检测无需窗口即可确定性验证。

### Config（数据驱动配置）

- INI 风格键值解析：`;` / `#` 注释、空行、缺 `=` 的坏行全部容错（记录警告后跳过）。
- 解析与取值分离（加载存字符串、读取才转数值）；默认值由调用方传入——文件缺失、缺键、非法值都不致命，游戏永远能正常启动。
- 字符串读取：`getString` 直接取原文（缺键 / 空值回退调用方默认值），服务输入绑定等字符串型配置。

### AudioSystem（音效）

- 基于 SDL3_mixer：6 个 WAV 在启动时一次性预解码到内存，每个音效绑定独立轨道、互不打断，重复触发从头重播。
- RAII 生命周期（`destroy` 幂等 + 析构兜底）；无音频设备时整体静默、单个素材失败仅该项静默，`play()` 在任何状态下都是安全调用。

### Font 与 DebugText（文本渲染）

- `Font`：SDL3_ttf 的 RAII 封装，支持 UTF-8 中文文本的加载 / 测量 / 绘制（含水平居中版）；加载失败后所有绘制调用安全跳过。
- `DebugText`：header-only 的调试字体助手（SDL 内置 8x8 字体，仅 ASCII），采用屏幕像素坐标、内部自动处理渲染缩放。

### Vec2（数学层起步）

- header-only 的二维向量（`include/engine/math/`）：构造、加减、标量乘除、点积、长度 / 长度平方、归一化（零向量保护）、两点距离与线性插值。
- 「一对浮点」的位置 / 速度 / 位移逐步收编为类型化运算；`Vec3` / `Mat4` 随 3D 需求到来再补，接口风格保持一致（架构红线①：header-only、渐进迁移）。

### Camera（世界 <-> 屏幕变换与平滑跟随）

- header-only 的 2D 相机（`include/engine/Camera.h`）：把「世界坐标」与「屏幕坐标」的换算收敛到一处——实体存世界坐标，绘制前 `worldToScreen`；鼠标输入经 `screenToWorld` 回到世界。
- `snapTo(cx,cy)` 立即定位；`follow(cx,cy,dt,smoothTime)` 指数平滑跟随（`factor = 1 - exp(-dt / smoothTime)`，公式蕴含 dt、帧率无关、无过冲）。
- 每轴自动钳制：世界不小于视口时钳在 `[0, world - viewport]`（看不到世界外虚空）；世界小于视口时该轴居中。
- 3D 前瞻：相机本质 = 世界 -> 屏幕的坐标变换；未来 3D 时内部升级为视图 x 投影矩阵，「实体存世界坐标、绘制处换算」的纪律与接口语义保持不变。

### Rect（碰撞工具）

- `rectsOverlap()`：header-only 的 AABB 相交判定（分离轴思想），约定「边界刚好贴合不算碰撞」。

## 目录结构

```
GameEngine/
├─ CMakeLists.txt          # 静态库工程定义（target：GameEngine）
├─ include/engine/         # 公开头文件（宿主以 "engine/xxx.h" 包含）
│  ├─ Entity.h             # 实体基类：位置尺寸 / 帧动画（帧区间）/ 三级回退 / 插值快照 / 翻转 / 碰撞盒 / 相机化渲染
│  ├─ Camera.h             # 2D 相机：世界↔屏幕换算 / 指数平滑跟随 / 世界边界钳制（header-only）
│  ├─ Scene.h              # 场景基类：五个生命周期回调 + 注入接口
│  ├─ SceneManager.h       # 场景管理器：唯一所有权 + 延迟切换 + 帧末结算
│  ├─ UpdateContext.h      # 固定步长世界上下文（dt / 键盘 / 视口 / 输入系统）
│  ├─ Input.h              # 输入系统：动作映射 / 改键 / 边沿检测（键盘 + 鼠标，held/pressed/released）
│  ├─ Config.h             # INI 风格配置解析（容错 + 默认值回退）
│  ├─ AudioSystem.h        # 音效系统（RAII + 全链路降级）
│  ├─ Font.h               # TTF 文本加载 / 测量 / 绘制（RAII）
│  ├─ DebugText.h          # 调试文字助手（header-only，仅 ASCII）
│  ├─ Rect.h               # AABB 相交判定（header-only）
│  └─ math/                # 数学层（header-only 起步）
│     └─ Vec2.h            # 二维向量：算术 / 点积 / 长度 / 归一化 / 插值
├─ src/                    # 实现文件
│  ├─ Entity.cpp           # 纹理回退（最近邻采样）/ 帧号换算（子区间）/ 插值绘制（相机换算）/ 翻转 / 碰撞盒换算
│  ├─ SceneManager.cpp     # 延迟切换与事件 / 更新 / 渲染转发
│  ├─ Config.cpp           # 逐行解析与坏行容错
│  ├─ Input.cpp            # 动作绑定与边沿推进（键盘 + 鼠标同路径；update / 注入入口）
│  ├─ AudioSystem.cpp      # SDL3_mixer 初始化 / 加载 / 播放 / 释放
│  └─ Font.cpp             # SDL3_ttf 加载与文本绘制
├─ .gitignore              # 忽略构建产物（out/、build/、*.obj 等）
└─ README.md               # 本文件
```

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows（当前开发与验证环境） |
| 编译器 | MSVC（源码含中文注释，构建脚本自动添加 `/utf-8`） |
| 构建系统 | CMake ≥ 3.20 |
| 语言标准 | C++20 |
| SDL 组件 | 由宿主提供，见下述「依赖契约」 |

配套游戏当前使用：SDL3 3.5、SDL3_image 3.4.8、SDL3_ttf 3.2.2、SDL3_mixer 3.2.4。

## 依赖契约（重要）

引擎**不内嵌** SDL 源码。宿主必须**先**提供四个 SDL 组件目标（例如以 `add_subdirectory` 引入源码），**再**接入引擎，避免同一套 SDL 被重复构建：

| CMake 目标 | 用途 | 链接传播 |
| --- | --- | --- |
| `SDL3::SDL3` | 窗口 / 渲染 / 事件基础 | PUBLIC —— 随引擎头文件传递给宿主 |
| `SDL3_image::SDL3_image` | PNG 纹理加载 | PRIVATE —— 仅引擎内部实现 |
| `SDL3_ttf::SDL3_ttf` | TTF 文本渲染 | PRIVATE —— 仅引擎内部实现 |
| `SDL3_mixer::SDL3_mixer` | WAV 音效播放 | PRIVATE —— 仅引擎内部实现 |

顺序错误（宿主未先提供 `SDL3::SDL3`）会在**配置期**直接 `FATAL_ERROR` 并给出修复提示，而不是拖到链接阶段才报出一堆费解的符号缺失。

include 约定：引擎以 `include/` 为 PUBLIC include 目录导出，引擎与宿主统一写 `#include "engine/xxx.h"`：

```cpp
#include "engine/Scene.h"       // 场景基类
#include "engine/Entity.h"      // 实体基类
#include "engine/AudioSystem.h" // 音效系统
```

宿主业务代码不需要为接入引擎修改任何 include 路径。

## 接入指南

### 方式一：git 子模块（推荐）

```bash
# 把引擎作为子模块放进宿主仓库（路径可自定，此处沿用配套游戏约定 libs/GameEngine）
git submodule add https://github.com/xrui-cmd/gameEngine.git libs/GameEngine
```

宿主的 CMakeLists.txt 三段式：

```cmake
# ① 先提供四个 SDL 组件（此处以源码 add_subdirectory 为例）
add_subdirectory(vendor/SDL-main EXCLUDE_FROM_ALL)   # SDL3 主库
add_subdirectory(vendor/SDL3_image EXCLUDE_FROM_ALL) # PNG 图片解码
add_subdirectory(vendor/SDL3_ttf EXCLUDE_FROM_ALL)   # TTF 字体渲染
add_subdirectory(vendor/SDL3_mixer EXCLUDE_FROM_ALL) # 音效播放

# ② 再接入引擎（顺序关键：引擎配置期会校验 SDL3::SDL3 目标是否存在）
add_subdirectory(libs/GameEngine)

# ③ 宿主程序链接引擎静态库（SDL3::SDL3 经 PUBLIC 自动传递，无需重复链接）
target_link_libraries(GameDev PRIVATE GameEngine)
```

克隆含子模块的宿主仓库时：

```bash
git clone --recursive <宿主仓库地址>           # 克隆时一并拉取子模块
git submodule update --init --recursive        # 或事后初始化
```

### 方式二：本地目录

把引擎源码（或本地 clone）放在宿主任意位置，直接 `add_subdirectory(<引擎路径>)` 接入，其余步骤与方式一相同。

### 最小使用示意

> 以下为**示意用伪代码骨架**（省略了错误处理与部分头文件包含），展示实体子类化与场景编写的典型写法。

自定义实体：构造时固定尺寸，`load()` 提供素材，覆写 `update` 时遵守首行快照纪律：

```cpp
class Star : public Entity
{
public:
  Star()
  {
    m_w = 32.0f; // 构造时固定宽（尺寸的唯一定义处）
    m_h = 32.0f; // 构造时固定高
  }

  bool load(SDL_Renderer* renderer)
  {
    // 三级回退：4 帧精灵表 -> 单帧图 -> 程序化金色方块
    return loadTextureWithFallback(renderer,
                                   "assets/star_sheet.png",      // 首选：精灵表
                                   "assets/star.png",            // 次选：单帧图
                                   4,                            // 精灵表总帧数
                                   0.12f,                        // 每帧 0.12 秒
                                   SDL_Color{255, 215, 0, 255}); // 兜底：金色
  }

  void update(const UpdateContext& ctx) override
  {
    snapshotPreviousPosition();    // 纪律：覆写 update 的第一行必须先快照
    m_x += 120.0f * ctx.deltaTime; // 以 120 像素/秒向右漂移（速度 × 固定步长）
  }
};
```

自定义场景：`onEnter` 保存 renderer、加载资源；`update` 分发上下文；`render` 按 alpha 插值绘制：

```cpp
class GameScene : public Scene
{
public:
  void onEnter(SDL_Renderer* renderer, const Config& config, AudioSystem& audio) override
  {
    m_renderer = renderer; // 保存 renderer：加载纹理与渲染都要用到
    m_audio = &audio;      // 借用音效系统（所有权在宿主，受管理期间必然非空）
    m_star.load(renderer); // 加载本场景资源（三级回退在 Entity 内部完成）
  }

  void update(const UpdateContext& ctx) override
  {
    m_star.update(ctx); // 把世界上下文分发给场上实体

    if (m_star.rect().x > ctx.viewportWidth) // 示例规则：星星漂出右边界
    {
      m_audio->play(SoundId::Record);                              // 触发音效（借用指针）
      m_sceneManager->requestScene(std::make_unique<MenuScene>()); // 请求切换：帧末才结算
    }
  }

  void render(float alpha) override
  {
    // alpha 在 [0,1)：插值位置先算世界坐标，再经相机换算到屏幕绘制。
    // 相机由场景持有：每固定步 follow 玩家中心、渲染时按值传入实体
    m_star.render(m_renderer, alpha, m_camera);
  }

private:
  SDL_Renderer* m_renderer = nullptr; // 只借用，不持有所有权
  AudioSystem* m_audio = nullptr;     // 同上
  Star m_star;                        // 实体按值持有
  Camera m_camera;                    // 相机按值持有（onEnter 设置视口/世界尺寸，每步 follow）
};
```

## 设计要点

### 固定步长与插值渲染的配合

固定步长主循环在宿主（见「配套项目」），引擎一侧遵守以下纪律：

1. **逻辑只认固定步**：每步 `update(ctx)` 以恒定 `ctx.deltaTime` 推进；碰撞判定使用逻辑位置（`rect()`），保证确定性。
2. **一步一头快照**：每步开始时 `snapshotPreviousPosition()` 记下起点，本步结束后的新位置成为插值终点。
3. **帧末换算 alpha**：宿主把「未消耗的时间余额 ÷ 固定步长」得到 [0,1) 的 `alpha`，传入 `render(alpha)`。
4. **渲染只做插值**：`render` 在上一固定步位置与当前位置之间线性插值（世界坐标），不修改任何逻辑状态（因此是 `const` 方法）。
5. **绘制处换算屏幕**：插值得到的世界坐标再经 `camera.worldToScreen` 换算为屏幕坐标绘制——判定用世界坐标、渲染用屏幕坐标，两侧由相机一处连接。
6. **瞬移必对齐**：`setPosition` 内部已同步快照；自定义瞬移（如随机落点重生）必须在落点定稿后手动调用 `snapshotPreviousPosition()`，否则渲染会从瞬移前的位置「拉丝」滑过去。

不启用插值时回退也很简单：`alpha` 传 0，画面即固定步位置——逻辑正确性不受影响。

### 资源三级回退

实体纹理加载逐级降级，任何一级成功即返回：

1. **精灵表**（首选）：多帧动画，帧数由调用方声明；
2. **单帧图**（次选）：帧数退化为 1，动画自然静止；
3. **程序化纯色方块**（兜底）：尺寸取实体自身 `m_w` / `m_h`、颜色取 `fallbackColor`——素材全缺也能看见实体、正常游玩。

每次降级都会记录一条 `SDL_LogWarn` 说明原因，便于定位素材问题。素材路径按「相对 exe 目录」传入，引擎内部拼接 `SDL_GetBasePath()`。

### 失败降级契约（总览）

| 环节 | 失败情形 | 行为 |
| --- | --- | --- |
| 音频 | 初始化失败 / 无播放设备 | 警告一次，整体静默；`play()` 为安全空操作 |
| 音频 | 单个 WAV 缺失 / 损坏 | 仅该音效静默，其余照常 |
| 字体 | `Font::load` 失败 | 警告一次；后续绘制 / 测量安全跳过（UI 无文字） |
| 配置 | 文件缺失 / 坏行 / 缺键 / 非法值 | 警告后回退调用方给出的默认值 |
| 纹理 | 精灵表与单帧图都缺失 | 降级为程序化纯色方块 |

统一原则：**资源问题不让游戏崩溃**——降级后逻辑、渲染、UI 一切照旧，只是可用的表现力减少。

## 配套项目

**COIN RUSH（金币冲刺）** —— 本引擎的首个使用方与抽取来源（本地 GameDev 仓库）：

- 宿主应用外壳：SDL 生命周期、窗口 / 渲染器、**固定步长主循环**（1/120 秒补步、delta 钳制、alpha 插值渲染、场景帧末结算）；
- 玩法层：主菜单 / 对局 / 暂停 / 结束场景，玩家 / 金币 / 敌人实体，最高分存档；
- 素材管线（`assets/`）与配置文件（`config/game.ini`）；
- 单元测试（`tests/`）：24 条碰撞用例 + 31 条配置断言（外加 10000 组随机性质测试），无需游戏窗口即可运行。

想了解各 API 的完整用法，COIN RUSH 的源码是最完整的示例。

## 开发约定

- **中文教学注释**：每处设计决策都说明「为什么」，而不是复述「是什么」；
- **命名**：类型名 PascalCase（`Entity`、`SceneManager`）；方法名小驼峰（`snapshotPreviousPosition`、`drawCentered`）；成员变量 `m_` 前缀 + 小驼峰（`m_renderer`、`m_animTime`）；
- **头文件包含卫宏**：`__XXX_H__` 形式（如 `__ENTITY_H__`）。

## 版本与来源

- 本仓库由配套游戏仓库（GameDev）于 **2026-10** 抽取引擎层而成，含引入以来的完整提交历史，可用 `git log` 追溯每处设计决策的来龙去脉；
- 当前仅 `main` 分支、暂无标签；建议使用者以具体**提交哈希**钉住版本（后续可按需发布标签）。

## 许可

暂未指定开源许可（All rights reserved）。如后续确定许可证，将在此处补充。
