#ifndef __SCENE_H__
#define __SCENE_H__

#include <SDL3/SDL.h>             // SDL_Renderer / SDL_Event：场景接口的基础类型
#include "engine/Config.h"        // Config：onEnter 注入的配置数据（可调数值的读取入口）
#include "engine/UpdateContext.h" // UpdateContext：update 的「世界上下文」参数

// 前置声明：场景通过管理器请求切换（完整定义见 engine/SceneManager.h）。
// 这里只用到指针，不需要完整类型，避免两个头文件互相包含
class SceneManager;

// 前置声明：音效系统由 onEnter 注入（完整定义见 engine/AudioSystem.h）。
// 这里同样只以引用/指针形式出现，不需要完整类型
class AudioSystem;

// ============================================================
// Scene：场景基类（引擎层）
// ============================================================
//
// 背景：GameDev 既当「应用外壳」（SDL 生命周期、主循环）又当「游戏逻辑」
// （玩家/金币/计分/倒计时/HUD）。加入主菜单、暂停后，外壳里若再堆
// 一组菜单状态与分支，改动就会互相牵连。于是把「一屏可玩内容」抽象成
// 场景（Scene）：
//
//   - 每个场景自己管自己的资源（在 onEnter 加载、析构时释放）
//   - 应用外壳只负责 SDL 生命周期与主循环，把事件/更新/渲染转发给
//     SceneManager，由它再转发给当前场景
//
// 生命周期约定（由 SceneManager 保证调用时机）：
//   onEnter     进入场景时调用一次：保存 renderer、读取配置、加载本场景
//               纹理资源
//   onExit      离开场景时调用一次：收尾钩子（本项目的资源释放放在
//               析构里，默认空实现即可满足；将来需要再覆写）
//   handleEvent/update/render  每帧转发；MenuScene 这类无逻辑场景
//               可以不覆写 update（基类默认为空操作）
//
// 为什么 onEnter 拿到的是 SDL_Renderer* 而不是窗口：
//   纹理由 renderer 创建、也必须先于 renderer 销毁；场景保存 renderer
//   指针用于「每次进入时加载」。确实需要窗口信息时，可以用
//   SDL_GetRenderWindow(renderer) 反查（PlayScene 就是这样做的）。
//
// 为什么 onEnter 还要拿到 Config：
//   场景是「可调数值」的消费方（如对局时长、金币数量）。配置由应用外壳
//   在启动时加载一次，经 SceneManager 在场景上位时注入——场景在 onEnter
//   里把关心的项读进自己的成员，之后每帧逻辑（update/restart）使用成员，
//   无需再触碰配置文件。
//
// 为什么 onEnter 还要拿到 AudioSystem：
//   音效是「动作式」能力：场景只在事件发生时（收币/暂停/结束等）调用
//   play(id)，不需要数据副本。所有权在 GameDev（生命周期覆盖全部场景），
//   经 SceneManager 在场景上位时原样转发——与 Config 走同一条注入管线；
//   场景保存其指针，供后续 handleEvent/update 中随时触发播放。
//
// 场景切换请求：
//   场景通过 m_sceneManager（由 SceneManager 在场景上位时注入）调用
//   requestScene(...) 请求切换；真正切换由 SceneManager 在帧末统一结算
// ============================================================

class Scene
{
public:
    // 虚析构：SceneManager 通过基类指针销毁场景，派生类的
    // 资源释放代码（纹理）才能被正确调用
    virtual ~Scene() = default;

    // 由 SceneManager 注入自身指针（场景不持有其所有权）。
    // 场景借此请求切换到别的场景（如菜单按 Enter 后进入对局）
    void setSceneManager(SceneManager* manager) { m_sceneManager = manager; }

    // 进入场景：保存 renderer、读取配置、加载本场景所需资源。
    // config 由 SceneManager 注入（应用启动时加载好的只读数据）；
    // audio 是音效系统的注入入口（与 config 同一模式：所有权在 GameDev，
    // 场景只借用指针，用于触发收币/开始/结束/暂停等音效）。
    // 默认空实现：没有资源和配置需求的场景可以不覆写
    virtual void onEnter(SDL_Renderer* /*renderer*/, const Config& /*config*/, AudioSystem& /*audio*/) {}

    // 离开场景：切换前的收尾钩子。本项目的场景资源在析构中释放，
    // 默认空实现即可；将来需要「离开时存档」等逻辑时可以覆写
    virtual void onExit() {}

    // 处理单个 SDL 事件（按键等）。默认空实现：不关心事件的场景可忽略
    virtual void handleEvent(const SDL_Event& /*event*/) {}

    // 每帧更新：收到外壳装配好的世界上下文（deltaTime/键盘/视口）。
    // 默认空实现：主菜单这类无逻辑场景可以不覆写
    virtual void update(const UpdateContext& /*ctx*/) {}

    // 每帧渲染：用 onEnter 保存的 renderer 绘制本场景内容。
    // alpha 是「渲染时刻在两次固定步之间的进度」[0,1)，有实体的场景
    // 用它做位置插值（见 Entity::render）；纯静态界面（如菜单）可以忽略。
    // 约定：只负责「画」，不调用 SDL_RenderPresent（提交由外壳统一做）
    virtual void render(float /*alpha*/) {}

protected:
    // 注入的场景管理器：由 SceneManager 在场景上位时设置，
    // 受其管理期间必然非空；派生场景可经它请求下一次场景切换
    SceneManager* m_sceneManager = nullptr;
};

#endif // !__SCENE_H__
