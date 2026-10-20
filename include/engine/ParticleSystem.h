#ifndef __PARTICLESYSTEM_H__
#define __PARTICLESYSTEM_H__

#include <SDL3/SDL.h> // SDL_Color / SDL_Renderer / SDL_FRect：粒子颜色与矩形绘制的基础类型
#include <array>      // std::array：定长对象池（容量编译期固定，运行期零分配）
#include <cstdint>    // uint32_t：内部随机数状态与种子注入的整数类型

// 前置声明：渲染时只以引用读取相机（完整定义由 ParticleSystem.cpp 包含），
// 本头文件不引入 Camera 的完整定义（减少头依赖）
class Camera;

// ============================================================
// ParticleParams：一次爆发发射的全部可调参数
// ============================================================
//
// 设计意图：把「发射什么粒子」收敛为一个按值传递的参数包——场景侧只描述
// 观感（颜色 / 速度 / 寿命 / 尺寸 / 方向张角 / 重力 / 阻尼），粒子系统
// 负责逐粒随机化与生命周期推进。默认值是一团向上的白色小火花，方便
// 调用方只改关心的字段。
//
// 方向与张角语义（弧度制）：
//   - spreadRadians >= 2π：整圆随机（爆炸/受击火花，不关心朝向）；
//   - spreadRadians = 0   ：只沿 directionRadians 一条直线发射（喷流）；
//   - 其余                ：以 directionRadians 为中轴、总张角 spreadRadians
//                           的扇形内均匀随机（升级金雨朝上、法术定向喷射）。
struct ParticleParams
{
  SDL_Color color{ 255, 255, 255, 255 }; // 粒子基础颜色（alpha 由寿命比例再乘算淡出）
  float speedMin = 40.0f;   // 初速下限（像素/秒）
  float speedMax = 120.0f;  // 初速上限（像素/秒）；内部自动交换非法倒置范围
  float lifeMin = 0.25f;    // 寿命下限（秒）；<=0 的区间由实现夹到最小可见寿命
  float lifeMax = 0.50f;    // 寿命上限（秒）；<=0 时整次发射被拒绝（无可见时间）
  float sizeMin = 2.0f;     // 矩形边长下限（像素）
  float sizeMax = 4.0f;     // 矩形边长上限（像素）
  float directionRadians = 0.0f;   // 发射中轴方向（弧度；0 = 屏幕右方，-π/2 = 上方）
  float spreadRadians = 6.2831853f; // 总张角（弧度；默认 2π = 整圆随机）
  float gravityY = 0.0f;    // 纵向重力加速度（像素/秒²；正值向下，负值上飘）
  float damping = 0.0f;     // 速度阻尼系数（1/秒；指数衰减，0 = 无阻尼）
};

// ============================================================
// ParticleSystem：固定容量对象池的轻量 2D 矩形粒子系统
// ============================================================
//
// 为什么是「固定容量对象池」？
//   粒子是高频、短命、数量有界的表现层对象。每帧 new/delete 会造成堆碎片
//   与不可预测的停顿；这里用 std::array 一次分配 512 个槽位，发射只写
//   已释放槽位，更新只扫固定数组——运行期零分配、耗时有上界，符合
//   「表现层绝不拖累固定步逻辑」的引擎纪律。
//
// 池满策略（写清楚，避免调用方猜）：
//   发射时从游标 m_nextIndex 起环形扫描首个空闲槽位；若整池都活跃
//   （池满），直接覆盖游标处粒子——游标按发射顺序推进，因此被覆盖的
//   通常是最早发射、最接近寿命终点的粒子。语义 = 「新粒子优先，丢弃最旧」，
//   保证高密度事件（击杀+拾取+法术同帧）仍有可见反馈，而不是静默丢新粒子。
//
// 更新与渲染（职责边界）：
//   - update(dt)：只推进活跃粒子（寿命递减、重力、阻尼、位移）；dt <= 0
//     直接返回（暂停由宿主不调用实现，dt=0 也不会产生任何变化）；
//   - render(renderer, camera)：屏幕空间绘制——世界坐标经 Camera::worldToScreen
//     换算（含相机震屏偏移），按 life/maxLife 线性淡出 alpha；绘制用
//     SDL_BLENDMODE_BLEND 并保存/恢复渲染器状态，不污染后续绘制；
//   - 粒子系统不查询 SDL 窗口、不持有纹理：无窗口单测可覆盖池/寿命逻辑。
//
// 随机性：
//   内部 xorshift32 伪随机（可 setSeed 复现）；同一 seed + 同一发射序列
//   得到同一组粒子参数——冒烟/单测可确定性验证。种子 0 会被替换为非 0
//   常量（xorshift 的状态不能为 0）。
// ============================================================

class ParticleSystem
{
public:
  // 对象池容量（个）：512 足以覆盖「击杀 12 + 拾取 6 + 法术 18」这类同帧
  // 多事件叠加；超出按池满策略覆盖最旧粒子。容量是编译期常量，改大改小
  // 都无需改分配逻辑
  static constexpr int kCapacity = 512;

  // 单个粒子的运行时状态（公开结构：便于调试与单测直接读取字段；
  // 外部只读——发射/推进/清空必须经 ParticleSystem 的接口）
  struct Particle
  {
    float x = 0.0f;        // 位置 x（世界坐标，像素）
    float y = 0.0f;        // 位置 y（世界坐标，像素）
    float vx = 0.0f;       // 速度 x（像素/秒）
    float vy = 0.0f;       // 速度 y（像素/秒；屏幕坐标 y 向下为正）
    float life = 0.0f;     // 剩余寿命（秒；<=0 表示该槽位空闲）
    float maxLife = 0.0f;  // 初始寿命（秒；淡出比例 life/maxLife 的分母）
    float size = 0.0f;     // 矩形边长（像素；绘制时以位置为中心）
    float gravityY = 0.0f; // 本粒子的纵向重力（像素/秒²；发射时从参数快照）
    float damping = 0.0f;  // 本粒子的阻尼系数（1/秒；发射时从参数快照）
    Uint8 r = 255;         // 红色通道（0~255）
    Uint8 g = 255;         // 绿色通道
    Uint8 b = 255;         // 蓝色通道
    Uint8 a = 255;         // 基础不透明度（渲染时再乘寿命比例）
    bool active = false;   // 是否占用槽位（false = 可被下一次发射复用）
  };

  // 一次性爆发发射 count 个粒子到世界坐标 (x, y)：
  //   - count <= 0 / lifeMax <= 0：无可见粒子，直接返回；
  //   - 速度/寿命/尺寸的范围倒置自动交换，负值按下限夹取（调用方错误不崩）；
  //   - 方向按 params.directionRadians + 张角随机（整圆/扇形/直线三种语义）；
  //   - 池满时覆盖最旧粒子（策略见类头注释）
  void emitBurst(float x, float y, int count, const ParticleParams& params);

  // 推进所有活跃粒子 dt 秒：寿命递减 -> 重力加速 -> 阻尼衰减 -> 位移积分。
  // 寿命归零的粒子立即释放槽位；dt <= 0 不推进（防御暂停/零步长调用）
  void update(float dt);

  // 屏幕空间绘制全部活跃粒子：
  //   - 世界坐标经 camera.worldToScreen 换算（渲染路径包含震屏偏移）；
  //   - 颜色 alpha = 基础 alpha x (life/maxLife)，实现线性淡出；
  //   - 使用 BLEND 混合绘制矩形，结束后恢复渲染器原混合模式与绘制色
  void render(SDL_Renderer* renderer, const Camera& camera) const;

  // 清空全部粒子（重开/切场景）：所有槽位置为空闲，游标归零
  void clear();

  // 当前活跃粒子数（调试/单测/池满策略验证用）
  int aliveCount() const;

  // 注入随机种子（0 会被替换为非 0 常量）：同一 seed + 同一发射序列可复现
  void setSeed(uint32_t seed);

  // 只读访问整个粒子池（单测检查位置/寿命/颜色等字段；正常渲染不需要）
  const std::array<Particle, kCapacity>& particles() const { return m_particles; }

  // 池容量查询（单测断言与调用方容量规划）
  static constexpr int capacity() { return kCapacity; }

private:
  // xorshift32 推进一步并返回 32 位随机数（内部随机源；实现见 cpp）
  uint32_t nextRandom();

  // 均匀分布 [0,1) 的浮点随机数（由 nextRandom 取高 24 位换算）
  float random01();

  // 粒子槽位数组：定长对象池，运行期零分配（默认全部空闲）
  std::array<Particle, kCapacity> m_particles{};

  // 发射游标：下一次寻找空闲槽位的起点；池满时它指向「最旧」的覆盖目标。
  // 每次发射后推进到该槽位的下一格，形成环形扫描
  int m_nextIndex = 0;

  // 随机数状态：非 0 常量初值（xorshift 状态不能为 0；setSeed 可覆盖）
  uint32_t m_rng = 0x9E3779B9u;
};

#endif // !__PARTICLESYSTEM_H__
