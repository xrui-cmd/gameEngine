#include "engine/AudioSystem.h"
#include <SDL3_mixer/SDL_mixer.h> // MIX_Init / MIX_CreateMixerDevice / MIX_LoadAudio / MIX_PlayTrack 等
#include <string>                 // std::string：拼接音效文件的绝对路径

// ============================================================
// AudioSystem 的实现：初始化/降级、资源生命周期、播放。
// 头文件已说明设计背景，这里补充实现层面的关键决策：
//   - 所有 SDL_mixer 调用都在本文件内完成（头文件零 SDL_mixer 依赖）；
//   - destroy() 幂等：逐个判空释放并置空，重复调用成空操作；
//   - 失败降级的分级：前置失败（库/设备）整体降级；素材失败逐项降级；
//     播放失败只警告首次（避免每帧刷屏）。
// ============================================================

AudioSystem::~AudioSystem()
{
    // 析构兜底释放：即使调用方从未显式 destroy，音效资源也不会泄漏
    destroy();
}

bool AudioSystem::initialize()
{
    // 幂等：先释放可能残留的旧资源，保证重复初始化不会泄漏
    destroy();

    // 1) 库级初始化：登记内置解码器（本项目编译期只保留了 WAV）与全局状态。
    //    这是"整体降级"的第一道门槛——失败（环境异常）即警告并放弃音效
    if (!MIX_Init())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "音效初始化失败（%s）：游戏将静默运行", SDL_GetError());
        return false; // 未持有任何资源，直接返回失败
    }
    m_mixInitialized = true; // 记下"库已初始化"，供 destroy 配对 MIX_Quit

    // 2) 创建混音器并打开默认播放设备。MIX_CreateMixerDevice 内部会调用
    //    SDL_Init(SDL_INIT_AUDIO)；没有可用音频设备时返回空指针——
    //    这正是"无设备降级"的兜底点：警告一次后整个音效系统关闭，
    //    游戏逻辑、渲染、UI 不受影响，只是没有声音
    m_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (m_mixer == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "音频设备不可用（%s）：游戏将静默运行", SDL_GetError());
        return false; // 库级资源已由 destroy()（析构兜底）未来配对回收
    }

    // 3) 定位素材目录：与图片/字体同一模式——以 exe 所在目录为基准，
    //    拼接 <exe目录>/assets/sounds/<文件名>。拿不到基路径时六个音效
    //    全部走"逐项加载失败"分支，设备仍可用（返回值保持 true）
    const char* basePath = SDL_GetBasePath();
    if (basePath == nullptr)
    {
        // 只警告不中止：没有素材就是静默，设备与库保持就绪可继续复用
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "无法定位程序目录（%s）：音效素材将全部跳过", SDL_GetError());
        return true; // 设备就绪，但没有任何音效——play 会安全静默
    }

    // 4) 预加载六个音效并逐一建立播放轨道。任何一项失败都只警告并跳过
    //    该项（逐个降级），不放倒整个系统——例如只缺 coin.wav 时，
    //    其余五个音效照常工作，游戏流程完全不受影响
    for (int i = 0; i < kSoundCount; ++i)
    {
        // 当前项的音效 ID（枚举强转）：同时用于取文件名与日志短名
        const SoundId id = static_cast<SoundId>(i);

        // 音效文件绝对路径：<exe目录>/assets/sounds/coin.wav 等
        const std::string path = std::string(basePath) + "assets/sounds/" + soundFileName(id);

        // MIX_LoadAudio 第三个参数 predecode=true：整段解码到内存。
        // 短音效（几十 KB）选择预解码而非流式——播放时零 IO、零解码开销，
        // 且解码错误在加载阶段就暴露（而不是首次播放时才失败）
        MIX_Audio* audio = MIX_LoadAudio(m_mixer, path.c_str(), true);
        if (audio == nullptr)
        {
            // 逐项降级：警告一次并跳过该项，其余音效与游戏不受影响
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "加载音效失败（%s）：%s", path.c_str(), SDL_GetError());
            continue; // 该项的 audio/track 保持空，play 时安全跳过
        }

        // 为该音效创建独立播放轨道：不同音效各走各的轨道，互不打断。
        // （同一音效重复触发会重启本轨道，连收金币时"叮"声自然衔接）
        MIX_Track* track = MIX_CreateTrack(m_mixer);

        // 轨道创建失败或绑定失败：两项资源都要回收，避免泄漏，然后跳过该项
        if (track == nullptr || !MIX_SetTrackAudio(track, audio))
        {
            // 警告文案包含具体音效名，便于定位是哪一项降级
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "音效轨道建立失败（%s）：%s", path.c_str(), SDL_GetError());

            // 先销毁半成品轨道（若有），再销毁音频数据——逆序释放
            if (track != nullptr)
            {
                MIX_DestroyTrack(track); // 轨道引用音频，必须先于音频释放
            }
            MIX_DestroyAudio(audio); // 回收已解码的音频数据
            continue;                // 跳过该项，继续加载后面的音效
        }

        // 全部就绪：把两项资源登记到对应下标的资源组里（所有权移交）
        m_sounds[i].audio = audio; // 解码数据归本系统持有
        m_sounds[i].track = track; // 播放轨道归本系统持有
    }

    // 设备就绪即算"可用"：哪怕有素材失败，play 会对缺失项安全静默
    return true;
}

void AudioSystem::destroy()
{
    // 1) 逐个释放音效资源组：先轨道后音频（轨道引用音频，逆序释放），
    //    每个指针判空后置空，使本函数天然幂等（可重复调用、可提前调用）
    for (int i = 0; i < kSoundCount; ++i)
    {
        // 当前资源组的引用：直接改成员，无需回写
        Sound& sound = m_sounds[i];

        // 先释放播放轨道（存在才释放，避免对空指针调用 SDL_mixer）
        if (sound.track != nullptr)
        {
            MIX_DestroyTrack(sound.track); // 停止并销毁轨道
            sound.track = nullptr;         // 置空：二次调用时跳过
        }

        // 再释放解码后的音频数据
        if (sound.audio != nullptr)
        {
            MIX_DestroyAudio(sound.audio); // 释放内存中的 PCM 数据
            sound.audio = nullptr;         // 置空：二次调用时跳过
        }
    }

    // 2) 释放混音器：MIX_CreateMixerDevice 内部打开了默认播放设备，
    //    MIX_DestroyMixer 会一并关闭设备并退出 SDL 音频子系统
    //    （与 MIX_CreateMixerDevice 内部的 SDL_Init(SDL_INIT_AUDIO) 配对）
    if (m_mixer != nullptr)
    {
        MIX_DestroyMixer(m_mixer); // 关闭设备、停止全部音频输出
        m_mixer = nullptr;         // 置空：二次调用时跳过，同时让 isReady 归假
    }

    // 3) 库级收尾：与 initialize 的 MIX_Init 严格配对（引用计数语义）。
    //    只在 MIX_Init 真正成功过时调用，避免无效调用打乱计数
    if (m_mixInitialized)
    {
        MIX_Quit();               // 释放解码器等库级全局资源（同时兜底清理遗漏对象）
        m_mixInitialized = false; // 复位标记：允许将来再次 initialize
    }
}

void AudioSystem::play(SoundId id)
{
    // 冒烟观测点（先于一切判断）：每次调用都记录事件——"事件挂载是否正确"
    // 与"音频设备是否可用"相互独立，自动化冒烟测试据此断言四类音效触发
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "播放音效: %s", soundName(id));

    // 降级路径①：混音器不存在（初始化失败/无音频设备）——静默跳过。
    // 初始化阶段已警告过一次，这里不再重复记录
    if (m_mixer == nullptr)
    {
        return;
    }

    // 把强类型 ID 转成资源组下标；越界（防御性检查，正常枚举不会发生）
    // 直接返回，避免用坏下标访问数组
    const int index = static_cast<int>(id);
    if (index < 0 || index >= kSoundCount)
    {
        return;
    }

    // 降级路径②：该项音效加载失败（素材缺失/损坏）——轨道为空，静默跳过
    // （加载时已逐项警告过一次，这里不重复刷屏）
    Sound& sound = m_sounds[index];
    if (sound.track == nullptr)
    {
        return;
    }

    // 启动（或重启）该音效轨道。MIX_PlayTrack 对"已在播放"的轨道是
    // 从头重播——这正是连续收币时期望的听感，也保证重复调用永远安全。
    // options 传 0 表示默认参数（播完整段、不循环、无淡入淡出）
    if (!MIX_PlayTrack(sound.track, 0))
    {
        // 播放失败通常是环境级异常（设备抢占等）：只警告第一次，
        // 避免每次触发/每帧重复记录把日志刷爆
        static bool s_playErrorLogged = false; // 播放失败是否已记录（进程内一次）
        if (!s_playErrorLogged)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "音效播放失败：%s", SDL_GetError());
            s_playErrorLogged = true; // 后续失败不再重复记录
        }
    }
}

const char* AudioSystem::soundName(SoundId id)
{
    // switch 映射：枚举成员与日志短名严格一一对应。
    // 用短名而非中文的原因：日志便于脚本 grep/"机器断言"（如 coin/start）
    switch (id)
    {
    case SoundId::Coin:     // 收币
        return "coin";      // 与 assets/sounds/coin.wav 同名，便于对照
    case SoundId::Start:    // 开始/确认
        return "start";     // 与 assets/sounds/start.wav 同名
    case SoundId::GameOver: // 本局结束
        return "gameover";  // 与 assets/sounds/gameover.wav 同名
    case SoundId::Pause:    // 暂停/恢复
        return "pause";     // 与 assets/sounds/pause.wav 同名
    case SoundId::Hit:      // 被敌人撞中
        return "hit";       // 与 assets/sounds/hit.wav 同名
    case SoundId::Record:   // 打破最高分纪录
        return "record";    // 与 assets/sounds/record.wav 同名
    }

    // 防御分支：未知 ID（理论上不可能到达）给占位名，
    // 而不是返回空指针——日志格式化传入空指针是未定义行为
    return "unknown";
}

const char* AudioSystem::soundFileName(SoundId id)
{
    // 与 soundName 结构一致：枚举成员 -> 实际素材文件名。
    // 文件名带 .wav 后缀，与 assets/sounds/ 目录内容一一对应
    switch (id)
    {
    case SoundId::Coin:     // 收币音效文件
        return "coin.wav";  // 880->1320Hz 双段「叮」
    case SoundId::Start:    // 开始音效文件
        return "start.wav"; // 600->900Hz 上扬
    case SoundId::GameOver: // 结束音效文件
        return "gameover.wav"; // 440->330->220Hz 三连下行
    case SoundId::Pause:    // 暂停音效文件
        return "pause.wav"; // 660Hz 轻短提示
    case SoundId::Hit:      // 命中音效文件
        return "hit.wav";   // 160->70Hz 低频下行「闷响」
    case SoundId::Record:   // 破纪录音效文件
        return "record.wav"; // C5->E5->G5 上行三连音琶音
    }

    // 防御分支：未知 ID 返回一个必然加载失败的名字（该音效不会存在），
    // 保证调用方逻辑闭合，且不产生空指针
    return "unknown.wav";
}
