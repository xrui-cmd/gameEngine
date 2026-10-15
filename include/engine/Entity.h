#ifndef __ENTITY_H__
#define __ENTITY_H__

#include <SDL3/SDL.h>
#include "UpdateContext.h" // UpdateContext：update 的「世界上下文」参数

// 前置声明：render 只以引用接收相机（完整定义见 engine/Camera.h）。
// 不在此 include 相机头：实体头文件是高频包含方，前置声明把编译依赖
// 收敛到「真正需要调用 worldToScreen 的 Entity.cpp」
class Camera;

// ============================================================
// Entity：所有游戏实体的抽象基类（引擎层）
// ============================================================
//
// 背景：Player 与 Coin 原先各自复制了一整套「纹理管理 + 帧动画」代码，
// 同样的逻辑改一处要同步两处。本基类把公共能力收敛到一处：
//
//   1. 空间数据：位置 m_x/m_y、尺寸 m_w/m_h，以及 rect() 矩形查询
//      （自相机落地起：位置一律为「世界坐标」，判定与渲染在绘制处解耦）
//   2. 纹理生命周期：release() 幂等释放；析构函数自动兜底调用
//   3. 帧动画：时间轴 m_animTime + 帧号换算（与渲染帧率解耦）
//   4. 三级回退加载：精灵表 -> 单帧图 -> 程序化纯色（收敛为辅助方法）
//   5. 插值渲染：记录「上一固定步位置」m_prevX/m_prevY，render 按 alpha
//      在 prev->cur 之间线性插值，再经相机换算为屏幕坐标绘制
//      （详见 render 与 snapshotPreviousPosition）
//   6. 渲染增强：水平翻转绘制（setFlipped）、动画循环子区间
//      （setAnimationRange，跑动帧与站立帧拼一张表按状态切换）、纹理
//      最近邻采样（像素风素材整数倍放大保持像素锐利，不做线性糊化）
//
// 统一动画语义（与原先 Player/Coin 的规则完全一致）：
//   m_animTime 按固定步长累加；帧号 = int(m_animTime / m_frameTime) % m_frameCount
//   这个公式只出现在基类 render 里一处；m_frameCount == 1 时帧号恒为 0，
//   srcRect 即整张图——单帧图 / 程序化回退天然退化为静止画面，无需特判。
//
// 子类手册：
//   - 构造时固定 m_w/m_h（尺寸的唯一定义处，不再散落在成员默认值里）
//   - load() 只提供素材路径、帧参数与回退颜色，其余交给基类辅助方法
//   - 需要自定义行为时覆写 update：第一行必须调用 snapshotPreviousPosition()，
//     默认实现（无条件推进帧动画）已包含该调用
//   - render 默认按插值位置绘制，绝大多数实体直接继承即可
// ============================================================

class Entity
{
public:
    // 默认构造：位置在 (0,0)、纹理为空；尺寸由派生类构造时固定
    Entity() = default;

    // 虚析构：通过基类指针销毁派生对象时也能正确释放纹理；
    // 内部调用 release() 兜底，配合 GameDev::shutdown 的提前释放也不会重复销毁
    virtual ~Entity();

    // 释放纹理并置空；幂等（可重复调用），供 shutdown 阶段提前调用
    void release();

    // 每帧更新实体状态。默认实现：无条件推进帧动画时间轴（金币旋转等）；
    // 需要「按输入/条件驱动」的实体（如玩家移动）由子类覆写。
    // 注意：覆写时必须在本函数第一行调用 snapshotPreviousPosition()，
    // 否则渲染插值拿不到「这一步开始前的位置」
    virtual void update(const UpdateContext& ctx);

    // 按「上一固定步位置 <-> 当前位置」的线性插值绘制；alpha 是两步之间
    // 的进度 [0,1)。不修改自身数据（rect() 等碰撞查询仍用逻辑位置），
    // 因此是 const 方法。默认实现已覆盖「从精灵表裁当前帧 -> 画到插值位置」。
    // 相机参数（本批新增）：实体位置是世界坐标，绘制前在 render 内部用
    // camera.worldToScreen 换算到屏幕——「判定用世界坐标（rect()）、渲染
    // 在绘制处换算」的既有解耦纪律延续；子类覆写时必须把相机原样传递
    virtual void render(SDL_Renderer* renderer, float alpha, const Camera& camera) const;

    // 返回实体当前的轴对齐矩形（位置 + 尺寸的副本），供 AABB 碰撞检测使用。
    // 注意返回的是「逻辑位置」而不是插值后的渲染位置：碰撞在固定步内
    // 用确定的状态判定，与渲染平滑完全解耦
    SDL_FRect rect() const;

    // 把「上一固定步位置」对齐到「当前位置」（快照）。
    // 两类调用时机，缺一不可：
    //   1. 固定步 update 的第一行（含子类覆写）：本步结束后的新位置
    //      与开头的旧位置构成插值区间，渲染才能平滑过渡；
    //   2. 瞬移类操作之后：setPosition 内部已自动对齐；自定义瞬移
    //      （如 Coin::respawn 的随机落点）必须在落点定稿后手动调用，
    //      否则插值会从瞬移前的位置「拉丝」滑到新位置，出现拖影
    void snapshotPreviousPosition();

    // 把左上角瞬间移动到 (x, y)：不经过速度/时间累积，重开归位等场景使用。
    // 内部同步插值快照（prev=cur），保证瞬移不会在渲染上产生拉丝
    void setPosition(float x, float y);

    // 覆盖帧动画的每帧时长（秒）：场景用配置值调整动画节奏时调用。
    // 应在 load() 之后调用——load 会先把内置默认帧时长写入该字段，配置值
    // 晚一步落定才能覆盖它。非正值会被忽略并警告（帧号换算以它为除数）
    void setFrameTime(float frameTime);

    // 设置帧动画的循环子区间 [first, last]（含两端，帧号从 0 起）：
    //   - 不调用 = 默认全区间循环（帧号 0 ~ 帧数-1，即基类原有行为）；
    //   - first == last = 定格在该帧（区间长度为 1，帧号恒等于 first）；
    //   - 典型用法：把「跑动帧」与「站立帧」拼进同一张精灵表（如玩家
    //     run f0~f3 + idle f0 共 5 帧），移动时循环 run 子区间 [0,3]、
    //     静止时定格 idle 帧 [4,4]——不必为两种状态各加载一张表。
    // 帧数相关的越界（子区间落在实际帧数之外）不在设置时校验——加载顺序
    // 不定（可能先声明后加载、也可能回退成单帧图），统一由 render 的
    // 帧号换算做防御性夹取，保证任何调用顺序都不会越界或崩溃
    void setAnimationRange(int first, int last);

    // 设置水平翻转绘制：true = 左右镜像（SDL 翻转参数），false = 原始朝向。
    // 纯渲染层标记：碰撞 rect()、插值位置、攻击判定等逻辑一律按未翻转的
    // 数据计算，镜像只改变「画出来的样子」。服务「移动时朝向翻转」这类
    // 需求（本方法只存储标记，绘制时才消费）
    void setFlipped(bool flipped);

    // 当前是否处于水平翻转绘制状态（渲染层查询用；子类绘制附属特效时
    // 需要与本体朝向保持一致可用它）
    bool flipped() const { return m_flipped; }

    // 尺寸便捷查询（重开归位时计算窗口居中位置在用）
    float width() const { return m_w; }  // 宽（像素）
    float height() const { return m_h; } // 高（像素）

protected:
    // 三级回退纹理加载（收敛 Player/Coin 原先各自复制的加载代码）：
    //   1. 首选精灵表 sheetPath（成功时帧数 = sheetFrameCount）
    //   2. 次选单帧图 singlePath（帧数退化为 1，动画静止）
    //   3. 回退程序化纯色方块（尺寸取 m_w/m_h，颜色取 fallbackColor）
    // 每次降级都记 SDL_LogWarn 说明原因；返回 true 表示最终拿到可用纹理。
    // 路径参数是「相对 exe 目录」的字符串（如 "assets/player_sheet.png"），
    // 基类内部拼接 SDL_GetBasePath()，子类不需要关心路径拼接细节
    bool loadTextureWithFallback(SDL_Renderer* renderer,
                                 const char* sheetPath,    // 精灵表相对路径（首选）
                                 const char* singlePath,   // 单帧图相对路径（次选）
                                 int sheetFrameCount,      // 精灵表包含的总帧数
                                 float frameTime,          // 每帧显示时长（秒）
                                 SDL_Color fallbackColor); // 程序化回退颜色（RGBA）

    // ---------- 空间数据（protected：派生类的 update 需要直接读写） ----------
    float m_x = 0.0f; // 左上角 x 坐标（像素，当前固定步的逻辑位置）
    float m_y = 0.0f; // 左上角 y 坐标（像素，当前固定步的逻辑位置）
    float m_w = 0.0f; // 宽（像素），由派生类构造时固定
    float m_h = 0.0f; // 高（像素），由派生类构造时固定

    // ---------- 插值渲染数据 ----------
    // 上一固定步的位置：update 开头快照、瞬移操作后同步（见
    // snapshotPreviousPosition 的说明）。render 用 prev->cur 做线性插值
    float m_prevX = 0.0f; // 上一固定步的 x（插值起点；初值 0 与 cur 一致）
    float m_prevY = 0.0f; // 上一固定步的 y（同上）

    // ---------- 渲染数据 ----------
    SDL_Texture* m_texture = nullptr; // 纹理；load 成功后非空，未加载时为 nullptr
    bool m_flipped = false;           // 是否水平翻转绘制（setFlipped 设置；纯渲染层标记）

    // ---------- 帧动画数据 ----------
    // m_frameCount：纹理实际包含的帧数（精灵表 > 1；单帧图/程序化回退为 1）
    // m_frameTime ：每帧显示时长（秒），由 loadTextureWithFallback 落定
    // m_animTime  ：动画时间轴（秒），按真实流逝时间累加
    int m_frameCount = 1;     // 默认 1 帧：单帧语义对静止实体天然成立
    float m_frameTime = 0.0f; // 未加载纹理时无意义；load 成功路径必然写入正值
    float m_animTime = 0.0f;  // 初值 0 表示从第 1 帧开始

    // 动画循环子区间（setAnimationRange 设置）。约定 m_animLast == -1 为
    // 「全区间」（解析为 m_frameCount - 1）——成员默认值因此不依赖加载后
    // 才确定的帧数；first == last 时区间长度为 1，即定格该帧
    int m_animFirst = 0; // 子区间起始帧（0 起）
    int m_animLast = -1; // 子区间结束帧（含）；-1 = 全区间（= 帧数 - 1）
};

#endif // !__ENTITY_H__
