#ifndef __AUDIOSYSTEM_H__
#define __AUDIOSYSTEM_H__

// ============================================================
// AudioSystem：音效的加载与播放（引擎层，RAII + 全链路降级）
// ============================================================
//
// 背景：游戏此前只有画面没有声音。本类基于 SDL3_mixer（3.x 新 API：
// MIX_Init / MIX_CreateMixerDevice / MIX_LoadAudio / MIX_PlayTrack）提供
// 「一次加载、随处播放」的音效能力：
//
//   - 启动时一次性把 assets/sounds/ 下的 WAV 全部预解码到内存
//     （predecode=true：短音效不需要流式解码，播放时零 IO、零解码）；
//   - 每个音效绑定一条独立轨道（MIX_Track），互不打断；
//     同一音效重复触发时轨道从头重播（连收金币时「叮」声连续）；
//   - 场景层只持有 SoundId 枚举与 play(id) 一个入口，不接触任何
//     SDL_mixer 类型（前向声明隔离，见下）。
//
// 失败降级（本项目的核心设计承诺——任何一环失败都不崩游戏）：
//   1. MIX_Init 失败           -> 警告一次，整体降级为静默；
//   2. 无音频设备/设备打开失败 -> 警告一次，整体降级为静默
//      （MIX_CreateMixerDevice 返回空；例如 SDL_AUDIODRIVER 指向的
//        驱动不存在，或系统根本没有可用的播放设备）；
//   3. 单个 WAV 加载失败       -> 警告一次，仅该音效静默（其余照常）；
//   4. play() 在降级后调用     -> 安全空操作（只记事件日志，不出声）。
//   降级后游戏逻辑、渲染、UI 一切照旧，只是没有声音。
//
// 生命周期契约：
//   - initialize() 与 destroy() 配对使用；destroy() 幂等（可重复调用，
//     析构函数里也兜底调一次，调用方忘记收尾也不会泄漏）；
//   - destroy() 必须在 SDL_Quit 之前调用：MIX_DestroyMixer 内部会关闭
//     音频设备并 SDL_QuitSubSystem(SDL_INIT_AUDIO)，MIX_Quit 释放库级资源
//   - 本类不依赖窗口/渲染器，但与场景的关系是「场景消费方先释放」：
//     先清场景、再 destroy() 本系统更符合依赖逆序（场景只借用指针）。
//
// 为什么头文件只前置声明 MIX 类型：
//   SDL_mixer 的完整头文件较大且会拖入 SDL 音频细节；场景/管理器等
//   上层代码只需要知道「有这么个类、有个 play(id)」，不需要 MIX_*
//   类型。完整定义只在 AudioSystem.cpp 里包含。
// ============================================================

// 前置声明：MIX 家族类型在头文件里只以指针形式出现，不需要完整定义；
// 完整定义（typedef struct MIX_Mixer MIX_Mixer; 等）由 AudioSystem.cpp
// 包含 <SDL3_mixer/SDL_mixer.h> 获得
struct MIX_Mixer; // 混音器：持有音频设备与全部轨道
struct MIX_Audio; // 已解码音频：一个 WAV 文件对应一个实例
struct MIX_Track; // 播放轨道：把音频「送到」混音器输出

// 音效 ID：与 assets/sounds/ 下的六个 WAV 文件一一对应。
// 用强类型枚举而不是裸字符串/整数：调用处写 SoundId::Coin 有编译期检查，
// 打错名字直接编译失败，重命名音效也能一键全改
enum class SoundId
{
    Coin,     // 收币「叮」（880->1320Hz 双段，约 100ms）
    Start,    // 开始/确认（600->900Hz 上扬，约 120ms）
    GameOver, // 本局结束（440->330->220Hz 三连下行，约 600ms）
    Pause,    // 暂停/恢复轻音（660Hz，约 80ms）
    Hit,      // 被敌人撞中（160->70Hz 低频下行「闷响」，约 150ms）
    Record    // 打破最高分纪录（C5->E5->G5 上行三连音琶音，约 300ms）
};

class AudioSystem
{
public:
    AudioSystem() = default; // 初始为空系统（未初始化），initialize 后才有资源

    // 析构兜底：自动调用 destroy() 释放全部音效资源，
    // 即使调用方忘记显式收尾也不会泄漏（RAII）
    ~AudioSystem();

    // 禁止拷贝：持有独占的混音器/音频/轨道资源，浅拷贝会造成双重释放；
    // 需要共享时应传递本对象的指针或引用（Scene 就是这么做的）
    AudioSystem(const AudioSystem&) = delete;            // 删除拷贝构造
    AudioSystem& operator=(const AudioSystem&) = delete; // 删除拷贝赋值

    // 初始化音效系统：MIX_Init -> 创建混音器/打开默认播放设备 -> 预加载
    // 六个 WAV -> 建立轨道。返回是否「设备就绪」：
    //   - 任一前置环节失败（无设备等）返回 false 并已警告一次，游戏降级静默；
    //   - 单个音效文件缺失/损坏不影响返回值（逐项降级，其余照常）。
    // 幂等：重复调用会先释放旧资源再重新初始化
    bool initialize();

    // 立即释放全部音效资源（幂等：重复调用安全，供场景提前释放或析构兜底）。
    // 释放顺序：轨道 -> 音频 -> 混音器（含关闭音频设备）-> MIX_Quit
    void destroy();

    // 音效系统是否可用（initialize 成功且设备就绪）。
    // 注意：即使返回 true，个别音效仍可能因素材缺失而静默（逐项降级）
    bool isReady() const { return m_mixer != nullptr; }

    // 播放指定音效。安全契约（所有情况都不会崩溃、不会阻塞）：
    //   - 系统未初始化/无设备：只记录事件日志，静默返回；
    //   - 该音效加载失败：静默返回（加载时已警告过一次，不重复刷屏）；
    //   - 正在播放同一音效：轨道从头重播（连续触发的自然语义）。
    // 另外无论音效系统是否可用，本函数都会记录「播放音效: xxx」日志——
    // 这是冒烟测试断言「事件挂载正确」的观测点
    void play(SoundId id);

private:
    // 音效总数：与 SoundId 枚举成员数、assets/sounds/ 文件数三者一致，
    // 增删音效时三处同步修改（编译器会拦截枚举与数组不一致的疏漏）
    static constexpr int kSoundCount = 6;

    // 一个音效的运行时资源组：解码数据 + 播放轨道。
    // 分开持有的原因：MIX_Audio 是「数据」，MIX_Track 是「播放器」；
    // 同一数据理论上可创建多条轨道（本项目是一个音效一条，最简单直接）
    struct Sound
    {
        MIX_Audio* audio = nullptr; // 解码后的音频数据；加载失败时为空
        MIX_Track* track = nullptr; // 绑定了该音频的播放轨道；失败时为空
    };

    // SoundId -> 日志短名（如 "coin"）：用于「播放音效: xxx」与警告文案
    static const char* soundName(SoundId id);

    // SoundId -> 文件名（如 "coin.wav"）：与 assets/sounds/ 目录内容对应
    static const char* soundFileName(SoundId id);

private:
    MIX_Mixer* m_mixer = nullptr; // 混音器（持有音频设备）；空 = 未就绪/已降级

    // 六个音效的资源组：下标与 SoundId 强转的整数值一一对应，
    // 一次初始化后内容固定（加载失败项保持空，play 时静默跳过）
    Sound m_sounds[kSoundCount];

    // MIX_Init 是否成功过：决定 destroy 时是否需要配对调用 MIX_Quit。
    // MIX_Init 是引用计数语义，必须与 MIX_Quit 严格配对
    bool m_mixInitialized = false;
};

#endif // !__AUDIOSYSTEM_H__
