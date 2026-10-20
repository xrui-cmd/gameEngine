#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <algorithm>          // std::clamp：相机位置的边界夹取
#include <cmath>              // std::exp：指数平滑的衰减因子
#include "engine/math/Vec2.h" // Vec2：相机位置与世界↔屏幕坐标的类型化表示

// ============================================================
// Camera：2D 相机（世界坐标 <-> 屏幕坐标变换 + 平滑跟随 + 边界钳制）
// ============================================================
//
// 为什么需要它？
//   世界尺寸（3072x3072）远大于窗口（1280x720）后，「实体的坐标」与
//   「屏幕像素」不再重合：实体存世界坐标，渲染时才换算到屏幕。相机是
//   这一步换算的唯一集中实现——不添相机的话，每个绘制点都要自己减偏移，
//   鼠标瞄准、飘字定位、区块裁剪会各错各的。
//
// 坐标系语义（两套坐标，务必分清）：
//   - 世界坐标：以世界左上角为原点的像素坐标。逻辑（移动/碰撞/判定/生成）
//     一律使用世界坐标；
//   - 屏幕坐标：以窗口客户区左上角为原点的像素坐标。只有渲染与鼠标输入
//     出现在这个坐标系里；
//   - 变换（当前版本无缩放、无旋转，纯平移）：
//       screen = world - camera.position()   （worldToScreen）
//       world  = screen + camera.position()  （screenToWorld）
//
// 相机位置（position）的定义：视口左上角对应的世界坐标。
//   视口 = 相机在世界中框选出的可见矩形，尺寸由 setViewport 给出。
//   想让世界点 (cx,cy) 出现在视口中心？snapTo/follow(cx,cy) 即可——
//   内部自动换算「左上角 = 目标中心 - 视口一半」。
//
// 边界策略（每轴独立、自动钳制）：
//   - 世界不小于视口：位置钳在 [0, world - viewport]，视口永远完整落在
//     世界内（玩家永远看不到世界外的虚空）；
//   - 世界小于视口：该轴固定居中（位置 = (world - viewport)/2，可能为负
//     ——世界比窗口小时整体显示在屏幕中央）。
//
// 跟随模型（follow 的指数平滑）：
//   factor = 1 - exp(-dt / smoothTime)
//   position += (目标位置 - position) * factor
//   dt 恒定时这是一阶低通滤波：smoothTime 是时间常数（秒），越小越紧跟。
//   选指数平滑而非固定速度追赶：公式蕴含 dt，帧率/步长变化下行为一致，
//   且天然无过冲、无抖动。
//
// 震屏（M3·甲：addShake / update / shakeOffset）：
//   - addShake(振幅像素, 时长秒) 登记一次冲击；振幅随时长线性衰减，偏移
//     方向由「时间相位」驱动做圆周抖动（|偏移| <= 当前振幅，永不越界）；
//   - 只作用于渲染路径 worldToScreen——画面/世界随震动抖动；
//     screenToWorld 刻意不含震屏，鼠标世界瞄准保持稳定（取舍：震屏期间
//     准星与世界点最多有「当前振幅」的视觉偏差，换取操作不被抖动干扰）；
//   - update(dt) 由宿主每固定步调用（暂停/冻结时可不调用以冻结震屏）；
//     表现层抖动不改变 position()，边界钳制/跟随逻辑完全不受影响。
//
// 3D 前瞻（本类为何是「通用件」）：
//   相机本质 = 「世界 -> 屏幕」的坐标变换。当前 2D 版本只有平移；未来 3D
//   时同一位移将升级为视图矩阵 x 投影矩阵，「实体存世界坐标、绘制处换算」
//   的纪律不变，接口语义（worldToScreen / screenToWorld / 跟随 + 钳制）
//   保持稳定，只是 Vec2 换成 Vec3、加减法换成矩阵乘法。
//
// 使用约定（职责边界）：
//   宿主（场景）负责：按窗口尺寸 setViewport、按世界尺寸 setWorldSize、
//   每固定步 follow 玩家中心（或入场 snapTo 对正）；
//   相机不查询 SDL、不持有渲染资源——纯数据变换，可在无窗口环境单测。
//
// 存储选型：header-only（与 Vec2 / Rect 同属无状态依赖的轻量工具类）。
// ============================================================

class Camera
{
public:
  // 设置视口尺寸（窗口逻辑像素）。窗口尺寸变化时重新调用；
  // 内部会立即按新视口回钳一次位置，保证 position() 任何时刻都满足
  // 边界约定（否则要等下一次 follow 才恢复，且首帧就可能越界）
  void setViewport(float width, float height)
  {
    m_viewportWidth = width;   // 视口宽（像素）：可见矩形的横向尺寸
    m_viewportHeight = height; // 视口高（像素）：可见矩形的纵向尺寸
    // 视口变化后立刻回钳：窗口变大可能让原位置越界（世界-视口变小了）
    m_position.x = clampAxis(m_position.x, m_worldWidth, m_viewportWidth);   // 横轴回钳
    m_position.y = clampAxis(m_position.y, m_worldHeight, m_viewportHeight); // 纵轴回钳
  }

  // 设置世界尺寸（像素）。世界生成后调用一次即可；
  // 与 setViewport 同理，内部立即回钳——世界换小时旧位置不应残留越界
  void setWorldSize(float width, float height)
  {
    m_worldWidth = width;   // 世界总宽（像素）：钳制右边界 = 宽 - 视口宽
    m_worldHeight = height; // 世界总高（像素）：同上
    // 世界尺寸变化后立刻回钳（处理顺序可能先 setViewport 再 setWorldSize）
    m_position.x = clampAxis(m_position.x, m_worldWidth, m_viewportWidth);   // 横轴回钳
    m_position.y = clampAxis(m_position.y, m_worldHeight, m_viewportHeight); // 纵轴回钳
  }

  // 立即定位：把相机中心对准世界点 (centerX, centerY)（入场/传送/重开时用，
  // 不经过平滑）。内部换算左上角后按边界钳制——中心点贴着地图角落时，
  // 相机自动停在最近的合法位置
  void snapTo(float centerX, float centerY)
  {
    // 左上角目标 = 目标中心 - 视口一半；两轴独立钳制
    m_position.x = clampAxis(centerX - m_viewportWidth * 0.5f, m_worldWidth, m_viewportWidth);   // 横轴：定位并钳制
    m_position.y = clampAxis(centerY - m_viewportHeight * 0.5f, m_worldHeight, m_viewportHeight); // 纵轴：同上
  }

  // 平滑跟随：每固定步调用，把相机中心以指数平滑逼近世界点 (centerX, centerY)。
  // dt 为固定步长（秒），smoothTime 为时间常数（秒）：
  //   factor = 1 - exp(-dt / smoothTime)
  // smoothTime <= 0 视为「不平滑」（factor = 1，立即到位，与 snapTo 等价）；
  // dt <= 0（调用方异常）时本步不动，避免出现反向平滑
  void follow(float centerX, float centerY, float dt, float smoothTime)
  {
    // 目标左上角：与 snapTo 同一换算（中心 -> 左上角）
    const float targetX = centerX - m_viewportWidth * 0.5f;  // 目标左上角 x（世界坐标）
    const float targetY = centerY - m_viewportHeight * 0.5f; // 目标左上角 y（世界坐标）

    // 本步平滑因子：dt 恒定时为常数，与帧率/步长变化共存（公式蕴含 dt）
    float factor = 0.0f; // 默认：dt <= 0 不推进（含 dt = 0 的零时间步）
    if (dt > 0.0f)
    {
      // smoothTime 为正走指数平滑；非正退化为「立即到位」（factor = 1）
      factor = (smoothTime > 0.0f) ? (1.0f - std::exp(-dt / smoothTime)) : 1.0f;
    }

    // 当前位置向目标插值一步，再按边界钳制（先平滑、后钳制：
    // 贴边时平滑仍在推进，不会把「已到边界」误当成「已到目标」）
    m_position.x = clampAxis(m_position.x + (targetX - m_position.x) * factor,
                             m_worldWidth, m_viewportWidth); // 横轴：平滑一步 + 钳制
    m_position.y = clampAxis(m_position.y + (targetY - m_position.y) * factor,
                             m_worldHeight, m_viewportHeight); // 纵轴：同上
  }

  // ---- 震屏（渲染层扰动；不影响相机位置/跟随/钳制）----

  // 登记一次震屏：amplitude = 最大偏移长度（像素），duration = 衰减总时长（秒）。
  // 叠加语义：已有震屏未结束时，只有「新振幅 >= 当前振幅」才覆盖并重新计时；
  // 更弱的震屏被忽略（避免小抖动打断大冲击），已结束则任何合法值都接受。
  // 参数非正（无振幅/无时长）视为无效，直接忽略
  void addShake(float amplitude, float duration)
  {
    // 入口防御：非正振幅或时长没有可表达的表现，忽略
    if (amplitude <= 0.0f || duration <= 0.0f)
    {
      return; // 不改变当前震屏状态
    }
    // 叠加语义：当前仍在震且新振幅更小 -> 忽略弱震（保住大冲击的主导权）
    if (m_shakeLeft > 0.0f && amplitude < m_shakeAmplitude)
    {
      return; // 弱震不覆盖强震
    }
    m_shakeAmplitude = amplitude; // 记录本次振幅（像素）
    m_shakeDuration = duration;   // 记录本次衰减总时长（秒；比例分母）
    m_shakeLeft = duration;       // 剩余时长置满（从此刻开始衰减）
    m_shakeTime = 0.0f;           // 相位归零（抖动的确定性起点）
  }

  // 推进震屏计时（宿主每固定步调用）：剩余时长递减、相位推进；
  // 到期后振幅清零（shakeOffset 返回 0）。dt <= 0 不推进（防御调用）
  void update(float dt)
  {
    // 无震屏或非正步长：无需推进
    if (m_shakeLeft <= 0.0f || dt <= 0.0f)
    {
      return; // 保持现状（含「无震屏」的零开销路径）
    }
    m_shakeTime += dt;                               // 抖动相位推进（决定偏移方向）
    m_shakeLeft = std::max(m_shakeLeft - dt, 0.0f);  // 剩余时长递减并夹到 0
    // 恰好到期：清振幅与分母（之后任何计算都走零偏移分支）
    if (m_shakeLeft <= 0.0f)
    {
      m_shakeAmplitude = 0.0f; // 振幅归零
      m_shakeDuration = 0.0f;  // 时长归零（防御除零）
    }
  }

  // 立即结束震屏（暂停/重开/过场）：状态全清零，下一帧偏移即为 0。
  // 与 addShake(…, 0) 不同：本方法无条件清掉正在进行的震屏
  void clearShake()
  {
    m_shakeAmplitude = 0.0f; // 振幅清零
    m_shakeDuration = 0.0f;  // 时长清零
    m_shakeLeft = 0.0f;      // 剩余时长清零（isShaking -> false）
    m_shakeTime = 0.0f;      // 相位归零（下次震屏从确定起点开始）
  }

  // 当前震屏偏移（屏幕像素）：未震屏时为 (0,0)。
  // 衰减模型 = 振幅 x (剩余时长 / 总时长)（线性）；
  // 偏移方向 = 相位驱动的单位圆（sin/cos），因此偏移长度恰好等于当前振幅，
  // 不会出现「斜向偏移比标称振幅更大」的问题
  Vec2 shakeOffset() const
  {
    // 未震屏 / 非法时长：零偏移（渲染路径的常见分支）
    if (m_shakeLeft <= 0.0f || m_shakeDuration <= 0.0f)
    {
      return Vec2{ 0.0f, 0.0f }; // 无抖动
    }
    // 线性衰减后的当前振幅（从满振幅平滑收敛到 0）
    const float amplitude = m_shakeAmplitude * (m_shakeLeft / m_shakeDuration);
    // 圆周抖动的相位（角速度 55 弧度/秒，约 8.8 圈/秒——短促冲击的观感）
    const float phase = m_shakeTime * 55.0f;
    return Vec2{ amplitude * std::sin(phase), // 横向偏移（单位圆 x 分量）
                 amplitude * std::cos(phase) }; // 纵向偏移（单位圆 y 分量）
  }

  // 是否正在震屏（渲染/调试查询；不改变状态）
  bool isShaking() const { return m_shakeLeft > 0.0f; }

  // 相机当前左上角位置（世界坐标）。渲染端可用它做可见区域裁剪：
  // 可见世界矩形 = position() 起、viewportWidth x viewportHeight 大。
  // 注意：这里返回逻辑位置，不含震屏偏移（震屏只作用于 worldToScreen）
  Vec2 position() const { return m_position; }

  // 世界坐标 -> 屏幕坐标（渲染路径：实体绘制、区块摆放都用它）。
  // 当前版本为「纯平移 + 震屏偏移」；将来加入缩放/旋转时只改本方法内部的换算。
  // 震屏偏移加在最后一步：所有世界层绘制（实体/区块/粒子/飘字）统一跟随抖动
  Vec2 worldToScreen(float worldX, float worldY) const
  {
    // 当前震屏偏移（未震屏时恒为零向量，无额外分支成本）
    const Vec2 shake = shakeOffset();
    // 屏幕点 = 世界点 - 视口左上角 + 震屏偏移
    return Vec2{ worldX - m_position.x + shake.x, worldY - m_position.y + shake.y };
  }

  // 屏幕坐标 -> 世界坐标（输入路径：鼠标瞄准等）。
  // 刻意不加震屏偏移：瞄准/交互坐标保持稳定——震屏只改变「看到的位置」，
  // 不改变「点到的位置」（取舍见类头注释）。无震屏时与 worldToScreen 精确互逆；
  // 震屏期间两者相差当前抖动偏移（这是设计语义，不是误差）
  Vec2 screenToWorld(float screenX, float screenY) const
  {
    // 世界点 = 屏幕点 + 视口左上角（与 worldToScreen 的平移部分成逆运算）
    return Vec2{ screenX + m_position.x, screenY + m_position.y };
  }

  // 视口尺寸查询（渲染裁剪、玩法范围的只读访问）
  float viewportWidth() const { return m_viewportWidth; }   // 视口宽（像素）
  float viewportHeight() const { return m_viewportHeight; } // 视口高（像素）

  // 世界尺寸查询（逻辑边界与容器边界的只读访问）
  float worldWidth() const { return m_worldWidth; }   // 世界总宽（像素）
  float worldHeight() const { return m_worldHeight; } // 世界总高（像素）

private:
  // 单轴钳制：世界不小于视口 -> 钳在 [0, world - viewport]；
  // 世界小于视口 -> 固定居中（返回值可能为负，代表世界左/上边位于屏幕内）。
  // 两轴共用同一规则，避免横纵各写一遍而漏改一处
  static float clampAxis(float position, float worldSize, float viewportSize)
  {
    // 世界不大于视口：该轴整体居中（世界完整可见，留白两侧等宽）
    if (worldSize <= viewportSize)
    {
      return (worldSize - viewportSize) * 0.5f; // 居中位置（<= 0）
    }
    // 常规情形：位置钳在有效区间 [0, world - viewport]
    return std::clamp(position, 0.0f, worldSize - viewportSize);
  }

  // 相机左上角位置（世界坐标）：核心状态。构造默认 (0,0)——
  // 视口/世界都未设置时 position 恒为 (0,0)，任何查询都是安全数值
  Vec2 m_position{};

  float m_viewportWidth = 0.0f;  // 视口宽（像素）：setViewport 设置
  float m_viewportHeight = 0.0f; // 视口高（像素）：setViewport 设置
  float m_worldWidth = 0.0f;     // 世界总宽（像素）：setWorldSize 设置
  float m_worldHeight = 0.0f;    // 世界总高（像素）：setWorldSize 设置

  // ---- 震屏状态（渲染层扰动；与相机位置/跟随完全解耦）----
  // 衰减 = 振幅 x (剩余/总时长)：线性收敛，短促冲击不拖尾。
  // 偏移方向 = 相位驱动的单位圆，|偏移| <= 当前振幅
  float m_shakeAmplitude = 0.0f; // 本次震屏的初始振幅（像素；0 = 未震屏）
  float m_shakeDuration = 0.0f;  // 本次震屏的总时长（秒；比例分母）
  float m_shakeLeft = 0.0f;      // 剩余时长（秒；<=0 表示震屏结束）
  float m_shakeTime = 0.0f;      // 已震动的相位时间（秒；决定偏移方向）
};

#endif // !__CAMERA_H__
