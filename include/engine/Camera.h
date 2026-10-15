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

  // 相机当前左上角位置（世界坐标）。渲染端可用它做可见区域裁剪：
  // 可见世界矩形 = position() 起、viewportWidth x viewportHeight 大
  Vec2 position() const { return m_position; }

  // 世界坐标 -> 屏幕坐标（渲染路径：实体绘制、区块摆放都用它）。
  // 当前版本为纯平移；将来加入缩放/旋转时只改本方法内部的换算
  Vec2 worldToScreen(float worldX, float worldY) const
  {
    // 屏幕点 = 世界点 - 视口左上角（视口平移量即相机位置）
    return Vec2{ worldX - m_position.x, worldY - m_position.y };
  }

  // 屏幕坐标 -> 世界坐标（输入路径：鼠标瞄准等）。与 worldToScreen 互逆，
  // 两者往返（换算再换回）应精确还原（浮点加减的可逆性，有单测覆盖）
  Vec2 screenToWorld(float screenX, float screenY) const
  {
    // 世界点 = 屏幕点 + 视口左上角（与 worldToScreen 的减法成逆运算）
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
};

#endif // !__CAMERA_H__
