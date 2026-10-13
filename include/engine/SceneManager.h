#ifndef __SCENEMANAGER_H__
#define __SCENEMANAGER_H__

#include <SDL3/SDL.h>              // SDL_Renderer / SDL_Event：转发接口的基础类型
#include <memory>                  // std::unique_ptr：场景的唯一所有权
#include "engine/AudioSystem.h"    // AudioSystem：场景 onEnter 时注入的音效系统
#include "engine/Config.h"         // Config：场景 onEnter 时注入的配置数据
#include "engine/Scene.h"          // Scene：被管理的场景基类
#include "engine/UpdateContext.h"  // UpdateContext：update 转发参数

// ============================================================
// SceneManager：当前场景的唯一持有者 + 延迟切换调度器
// ============================================================
//
// 为什么切换要「延迟到帧末」（requestScene 只登记、applyPendingChange 才执行）：
//   场景切换的触发点都在场景自己的回调里（如对局结束按 M 请求回菜单）。
//   如果 requestScene 立刻析构旧场景，就等于在旧场景的成员函数执行期间
//   把 this 销毁——该函数返回后继续访问自身成员就是「使用已释放对象」。
//   把切换动作推迟到帧末（所有场景回调都返回之后），旧场景再析构就安全。
//
// 资源生命周期：
//   - SceneManager 构造时绑定 renderer、配置与音效系统（场景 onEnter
//     需要它们加载纹理、读取可调数值、触发音效）
//   - 切换流程：旧场景 onExit -> 旧场景立刻析构（此时 renderer 尚在，
//     它的纹理安全释放）-> 新场景 onEnter（加载新资源）
//   - 退出流程：外壳在销毁 renderer 之前调用 clear()，保证全部场景
//     （含尚未结算的待切场景）的纹理先于 renderer 释放
// ============================================================

class SceneManager
{
public:
    // 构造时绑定渲染器、配置与音效系统：之后每次场景切换，新场景的 onEnter
    // 都能立即拿到它们加载资源、读取可调数值、触发播放
    explicit SceneManager(SDL_Renderer* renderer, const Config& config, AudioSystem& audio);

    // 请求切换场景（只登记，不立即执行）。
    // 真正切换发生在帧末的 applyPendingChange()，原因见文件顶部说明
    void requestScene(std::unique_ptr<Scene> nextScene);

    // 帧末结算延迟切换：旧场景 onExit + 析构 -> 新场景 onEnter。
    // 没有待切场景时什么都不做（绝大多数帧走这条快速路径）
    void applyPendingChange();

    // 释放全部场景（当前场景 + 待切场景）。
    // 必须在 renderer 销毁之前调用，否则场景纹理的释放会变成悬空引用
    void clear();

    // ---- 以下三个转发给当前场景；场景为空时安全跳过 ----
    void handleEvent(const SDL_Event& event);
    void update(const UpdateContext& ctx);
    void render(float alpha); // 携带插值系数（渲染帧在两次固定步之间的进度）

private:
    SDL_Renderer* m_renderer = nullptr; // 加载场景资源用的渲染器（只借用，不持有所有权）

    // 配置数据的只读引用：所有权在 GameDev（其生命周期覆盖本管理器），
    // 这里仅借用；每次场景 onEnter 时原样转发
    const Config& m_config;

    // 音效系统的引用：所有权同样在 GameDev（生命周期覆盖本管理器），
    // 这里仅借用；每次场景 onEnter 时原样转发（与 m_config 同一管线）
    AudioSystem& m_audio;

    std::unique_ptr<Scene> m_currentScene; // 当前正在运行的场景
    std::unique_ptr<Scene> m_pendingScene; // 已请求、等待帧末结算的新场景
};

#endif // !__SCENEMANAGER_H__
