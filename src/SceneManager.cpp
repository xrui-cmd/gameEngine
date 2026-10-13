#include "engine/SceneManager.h"

// 构造：绑定 renderer 与配置。SceneManager 只借用两者，不持有所有权
SceneManager::SceneManager(SDL_Renderer* renderer, const Config& config)
    : m_renderer(renderer) // 初始化列表写入成员：构造完成后立即可用于场景切换
    , m_config(config)     // 配置只读引用：生命周期由 GameDev 保证
{
}

// 请求切换：仅把新场景存起来，等 applyPendingChange 在帧末统一结算
void SceneManager::requestScene(std::unique_ptr<Scene> nextScene)
{
    // 空指针请求直接忽略：避免把当前场景换成空指针导致后续崩溃
    if (nextScene == nullptr)
    {
        return;
    }

    // 同一帧内若被连续请求多次（例如一次事件同时触发两条路径），
    // 以最后一次为准；被覆盖的待切场景从未进入过，直接析构无副作用
    m_pendingScene = std::move(nextScene);
}

// 帧末结算：真正执行「旧场景退场 -> 新场景入场」的切换
void SceneManager::applyPendingChange()
{
    // 没有待切场景：绝大多数帧都在这里直接返回
    if (m_pendingScene == nullptr)
    {
        return;
    }

    // 1) 旧场景先走 onExit，再立刻析构。
    //    此刻 renderer 仍在，旧场景的纹理（背景/玩家/金币）可安全释放
    if (m_currentScene != nullptr)
    {
        m_currentScene->onExit(); // 退场钩子：给场景最后一次收尾机会
        m_currentScene.reset();   // 显式析构旧场景，确保它的纹理先于新场景加载被释放
    }

    // 2) 新场景上位：所有权移交后立即 onEnter 加载它自己的资源
    m_currentScene = std::move(m_pendingScene);
    // 注入管理器自身：场景拿到它才能请求下一次切换（如菜单 -> 对局）
    m_currentScene->setSceneManager(this);
    // 入场加载：renderer 与配置已绑定，场景在此加载纹理、读取配置并完成初始布置
    m_currentScene->onEnter(m_renderer, m_config);
}

// 释放全部场景：退出前必须调用（且必须在 renderer 销毁之前）
void SceneManager::clear()
{
    // 先释放待切场景：它从未 onEnter（没有纹理），但也要避免悬挂
    m_pendingScene.reset();
    // 再释放当前场景：其析构内部会释放纹理，此刻 renderer 尚在，安全
    m_currentScene.reset();
}

// 转发事件：场景为空（初始化之前/清理之后）时安全跳过
void SceneManager::handleEvent(const SDL_Event& event)
{
    // 只有持有场景时才转发；空场景表示应用尚未进入或已清场
    if (m_currentScene != nullptr)
    {
        m_currentScene->handleEvent(event);
    }
}

// 转发更新：同一帧内所有 update 都作用于「本帧的当前场景」，
// 切换带来的新场景从下一帧才开始更新（延迟切换的自然结果）
void SceneManager::update(const UpdateContext& ctx)
{
    // 只有持有场景时才转发；空场景表示应用尚未进入或已清场
    if (m_currentScene != nullptr)
    {
        m_currentScene->update(ctx);
    }
}

// 转发渲染：只画当前场景，不负责 SDL_RenderPresent（由应用外壳统一提交）
void SceneManager::render()
{
    // 只有持有场景时才转发；空场景表示应用尚未进入或已清场
    if (m_currentScene != nullptr)
    {
        m_currentScene->render();
    }
}
