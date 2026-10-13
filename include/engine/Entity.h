#ifndef __ENTITY_H__
#define __ENTITY_H__

#include <SDL3/SDL.h>
#include "UpdateContext.h" // UpdateContext：update 的「世界上下文」参数

// ============================================================
// Entity：所有游戏实体的抽象基类（引擎层）
// ============================================================
//
// 背景：Player 与 Coin 原先各自复制了一整套「纹理管理 + 帧动画」代码，
// 同样的逻辑改一处要同步两处。本基类把公共能力收敛到一处：
//
//   1. 空间数据：位置 m_x/m_y、尺寸 m_w/m_h，以及 rect() 矩形查询
//   2. 纹理生命周期：release() 幂等释放；析构函数自动兜底调用
//   3. 帧动画：时间轴 m_animTime + 帧号换算（与渲染帧率解耦）
//   4. 三级回退加载：精灵表 -> 单帧图 -> 程序化纯色（收敛为辅助方法）
//
// 统一动画语义（与原先 Player/Coin 的规则完全一致）：
//   m_animTime 按真实时间累加；帧号 = int(m_animTime / m_frameTime) % m_frameCount
//   这个公式只出现在基类 render 里一处；m_frameCount == 1 时帧号恒为 0，
//   srcRect 即整张图——单帧图 / 程序化回退天然退化为静止画面，无需特判。
//
// 子类手册：
//   - 构造时固定 m_w/m_h（尺寸的唯一定义处，不再散落在成员默认值里）
//   - load() 只提供素材路径、帧参数与回退颜色，其余交给基类辅助方法
//   - 需要自定义行为时覆写 update：默认实现＝无条件推进帧动画，
//     适合金币这类「永远在转」的实体；玩家「移动才播」则需要覆写
//   - render 默认按当前帧绘制，绝大多数实体直接继承即可
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
    // 需要「按输入/条件驱动」的实体（如玩家移动）由子类覆写
    virtual void update(const UpdateContext& ctx);

    // 按当前帧把纹理绘制到当前位置；不修改自身数据，因此是 const 方法。
    // 默认实现已覆盖「从横向精灵表裁当前帧 -> 画到 (m_x, m_y)」的全部逻辑
    virtual void render(SDL_Renderer* renderer) const;

    // 返回实体当前的轴对齐矩形（位置 + 尺寸的副本），供 AABB 碰撞检测使用
    SDL_FRect rect() const;

    // 把左上角瞬间移动到 (x, y)：不经过速度/时间累积，重开归位等场景使用
    void setPosition(float x, float y);

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
    float m_x = 0.0f; // 左上角 x 坐标（像素）
    float m_y = 0.0f; // 左上角 y 坐标（像素）
    float m_w = 0.0f; // 宽（像素），由派生类构造时固定
    float m_h = 0.0f; // 高（像素），由派生类构造时固定

    // ---------- 渲染数据 ----------
    SDL_Texture* m_texture = nullptr; // 纹理；load 成功后非空，未加载时为 nullptr

    // ---------- 帧动画数据 ----------
    // m_frameCount：纹理实际包含的帧数（精灵表 > 1；单帧图/程序化回退为 1）
    // m_frameTime ：每帧显示时长（秒），由 loadTextureWithFallback 落定
    // m_animTime  ：动画时间轴（秒），按真实流逝时间累加
    int m_frameCount = 1;     // 默认 1 帧：单帧语义对静止实体天然成立
    float m_frameTime = 0.0f; // 未加载纹理时无意义；load 成功路径必然写入正值
    float m_animTime = 0.0f;  // 初值 0 表示从第 1 帧开始
};

#endif // !__ENTITY_H__
