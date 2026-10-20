#include "engine/ParticleSystem.h" // ParticleSystem / ParticleParams：本编译单元的实现对象
#include "engine/Camera.h"         // Camera：worldToScreen 世界->屏幕换算（渲染路径含震屏偏移）
#include <algorithm>               // std::clamp / std::max / std::min：范围归一化与安全夹取
#include <cmath>                   // std::cos / std::sin / std::exp：方向分解与阻尼衰减

// 整圆语义的判定阈值（弧度）：张角接近 2π 时按整圆随机（避免浮点误差把
// 「整圆」误判成扇形，导致粒子全挤在某个缺口之外）。留 0.001 弧度余量
namespace
{
constexpr float kTwoPi = 6.28318530718f; // 2π（弧度）：整圆张角常量
constexpr float kFullCircleEpsilon = 0.001f; // 整圆判定余量（弧度）
} // 匿名命名空间结束

// 注入随机种子：0 会破坏 xorshift 的状态（永远输出 0），替换为非 0 常量——
// 调用方传 0 时仍能得到可用的随机序列（防御性，不崩不退化为固定方向）
void ParticleSystem::setSeed(uint32_t seed)
{
  m_rng = (seed != 0u) ? seed : 0x9E3779B9u; // 0 -> 黄金比例常量（非 0）
}

// xorshift32：三步移位异或推进随机状态并返回新值。
// 选它而不是 std::mt19937：状态只有一个 uint32_t、无堆分配、足够粒子使用，
// 且同一 seed 的序列跨平台一致（冒烟/单测可复现）
uint32_t ParticleSystem::nextRandom()
{
  uint32_t value = m_rng;     // 取出当前状态（本步的随机源）
  value ^= value << 13;       // 左移 13 位异或：高位扰动
  value ^= value >> 17;       // 右移 17 位异或：低位扰动
  value ^= value << 5;        // 左移 5 位异或：混合收尾（xorshift32 标准参数）
  m_rng = value;              // 写回状态：下一次调用从新状态继续
  return value;               // 交还 32 位随机数
}

// 均匀分布 [0,1)：取高 24 位（尾数精度足够，且避开低位质量较差的位），
// 乘以 1/2^24 换算为浮点。结果恒小于 1，可直接乘范围宽度
float ParticleSystem::random01()
{
  const uint32_t bits = nextRandom() >> 8;         // 取高 24 位随机位
  return static_cast<float>(bits) * (1.0f / 16777216.0f); // 24 位 -> [0,1)
}

// 一次性爆发发射：逐粒随机方向/速度/寿命/尺寸，写入空闲槽位。
// 池满策略：环形扫描不到空闲槽时覆盖游标处（最旧）粒子——「新粒子优先」
void ParticleSystem::emitBurst(float x, float y, int count, const ParticleParams& params)
{
  // 发射数量与寿命的入口防御：没有数量或没有可见时间，直接放弃本次发射
  if (count <= 0 || params.lifeMax <= 0.0f)
  {
    return; // 无可见粒子：不占用槽位
  }

  // 速度范围归一化：负速度没有语义（按 0），倒置范围自动交换（min <= max）
  const float speedMin = std::max(0.0f, std::min(params.speedMin, params.speedMax)); // 初速下限
  const float speedMax = std::max(0.0f, std::max(params.speedMin, params.speedMax)); // 初速上限

  // 寿命范围归一化：最小 0.001 秒保证 maxLife > 0（渲染淡出除法安全），
  // 倒置范围自动交换（调用方把 lifeMin/lifeMax 写反也能得到合理结果）
  const float lifeMin = std::max(0.001f, std::min(params.lifeMin, params.lifeMax)); // 寿命下限
  const float lifeMax = std::max(lifeMin, std::max(params.lifeMin, params.lifeMax)); // 寿命上限

  // 尺寸范围归一化：最小 0.5 像素（0 尺寸不可见，负尺寸无意义）
  const float sizeMin = std::max(0.5f, std::min(params.sizeMin, params.sizeMax)); // 边长下限
  const float sizeMax = std::max(sizeMin, std::max(params.sizeMin, params.sizeMax)); // 边长上限

  // 张角夹到 [0, 2π]：负张角按 0（直线），超过整圆按整圆（没有更大的方向范围）
  const float spread = std::clamp(params.spreadRadians, 0.0f, kTwoPi); // 本次发射的总张角
  // 整圆判定：张角贴近 2π 时按「任意方向」随机，避免出现一条随机缺口
  const bool fullCircle = spread >= kTwoPi - kFullCircleEpsilon; // 是否整圆随机

  // 逐粒发射：每次独立随机（方向/速度/寿命/尺寸），共享同一个参数包的颜色等常量
  for (int i = 0; i < count; ++i)
  {
    // 环形扫描空闲槽位：从游标起最多扫一整圈（容量固定，循环必然有界）
    int slot = -1; // 选中的槽位下标（-1 = 尚未找到）
    for (int offset = 0; offset < kCapacity; ++offset)
    {
      // 候选槽位 = 游标 + 偏移（环形取模，越过数组尾回到头部）
      const int candidate = (m_nextIndex + offset) % kCapacity;
      // 找到空闲槽位：记录并立即停止扫描（优先复用最靠近游标的空闲位）
      if (!m_particles[candidate].active)
      {
        slot = candidate; // 记录空闲槽位
        break;            // 扫描结束
      }
    }
    // 池满：覆盖游标处粒子（游标随发射推进，通常即最旧、最接近寿命终点的粒子）
    if (slot < 0)
    {
      slot = m_nextIndex; // 池满策略：丢弃最旧（覆盖）
    }

    // 取出目标槽位引用：下面逐字段写入（覆盖旧粒子即完成回收）
    Particle& particle = m_particles[slot];
    particle.active = true;          // 占用槽位（若原为旧粒子，此处完成覆盖）
    particle.x = x;                  // 发射位置 x（世界坐标）
    particle.y = y;                  // 发射位置 y（世界坐标）

    // 方向随机：整圆取 [0,2π)；扇形取「中轴 ± 半张角」内的均匀角度
    const float angle = fullCircle
      ? random01() * kTwoPi // 整圆：任意方向
      : params.directionRadians + (random01() * 2.0f - 1.0f) * (spread * 0.5f); // 扇形：中轴偏移

    // 初速在 [speedMin, speedMax] 内均匀取值
    const float speed = speedMin + random01() * (speedMax - speedMin);
    particle.vx = std::cos(angle) * speed; // 速度分解 x
    particle.vy = std::sin(angle) * speed; // 速度分解 y（屏幕 y 向下为正）

    // 寿命在 [lifeMin, lifeMax] 内均匀取值；maxLife 与 life 同值（淡出分母）
    particle.life = lifeMin + random01() * (lifeMax - lifeMin); // 剩余寿命（秒）
    particle.maxLife = particle.life;                           // 初始寿命快照

    // 边长在 [sizeMin, sizeMax] 内均匀取值
    particle.size = sizeMin + random01() * (sizeMax - sizeMin);

    // 本粒子的物理参数快照：重力/阻尼在发射后不随调用方参数变化
    particle.gravityY = params.gravityY; // 纵向重力（像素/秒²）
    particle.damping = std::max(0.0f, params.damping); // 阻尼系数（负值按 0）

    // 颜色逐通道写入（同一次爆发的粒子共享基础颜色，alpha 淡出在渲染时算）
    particle.r = params.color.r; // 红色通道
    particle.g = params.color.g; // 绿色通道
    particle.b = params.color.b; // 蓝色通道
    particle.a = params.color.a; // 基础不透明度

    // 游标推进到该槽位的下一格：池满时下一颗覆盖的将是紧随其后的旧粒子
    m_nextIndex = (slot + 1) % kCapacity;
  }
}

// 推进所有活跃粒子：寿命 -> 重力 -> 阻尼 -> 位移。
// 顺序说明：先扣寿命（到点即释放，避免「尸体的最后一步」仍绘制）；
// 再更新速度（重力/阻尼），最后用新速度积分位置——等价显式欧拉一步
void ParticleSystem::update(float dt)
{
  // 非正步长（暂停/零步长/异常调用）：不推进任何状态，返回
  if (dt <= 0.0f)
  {
    return; // 冻结语义：粒子保持原位与剩余寿命
  }

  // 扫描固定数组：活跃粒子逐一推进，空闲槽位只做一次 bool 判断
  for (Particle& particle : m_particles)
  {
    // 空闲槽位跳过（池内绝大多数槽位在低密度时为空）
    if (!particle.active)
    {
      continue; // 下一槽位
    }

    // 寿命递减：先结算存活，再决定是否继续运动
    particle.life -= dt;

    // 寿命耗尽：释放槽位并清零寿命（下游渲染/统计据 active 判断）
    if (particle.life <= 0.0f)
    {
      particle.life = 0.0f;    // 夹到 0（不留下负寿命）
      particle.active = false; // 归还槽位（下一次发射可复用）
      continue;                // 本粒子本步不再位移/绘制
    }

    // 重力：纵向速度按 gravityY 加速（正值向下、负值上飘）
    particle.vy += particle.gravityY * dt;

    // 阻尼：指数衰减 v *= exp(-damping*dt)——帧率无关的物理正确衰减；
    // 阻尼为 0 时跳过 exp 调用（常见路径省一次数学函数）
    if (particle.damping > 0.0f)
    {
      const float decay = std::exp(-particle.damping * dt); // 本步速度保留比例
      particle.vx *= decay;                                 // 水平速度衰减
      particle.vy *= decay;                                 // 垂直速度衰减
    }

    // 位置积分：用更新后的速度推进（显式欧拉；1/120 固定步下误差可忽略）
    particle.x += particle.vx * dt; // 水平位移
    particle.y += particle.vy * dt; // 垂直位移
  }
}

// 绘制全部活跃粒子：世界->屏幕换算 + 寿命淡出 + 半透明矩形。
// 渲染器状态保存/恢复：颜色与混合模式都是全局绘制状态，粒子画完必须还原，
// 否则会污染 HUD 文字、区块烘焙等后续绘制（引擎内已有多次同类纪律）
void ParticleSystem::render(SDL_Renderer* renderer, const Camera& camera) const
{
  // 空渲染器（无窗口/初始化失败）：安全空操作，不崩
  if (renderer == nullptr)
  {
    return; // 无可绘制目标
  }

  // 保存调用前的混合模式（恢复时按原值写回；默认可能是 NONE 或 BLEND）
  SDL_BlendMode previousBlend = SDL_BLENDMODE_NONE; // 旧混合模式（默认值仅为初始化）
  SDL_GetRenderDrawBlendMode(renderer, &previousBlend); // 读取当前混合模式

  // 保存调用前的绘制颜色四通道（粒子逐粒改色，画完必须还原）
  Uint8 previousR = 0; // 旧红色通道
  Uint8 previousG = 0; // 旧绿色通道
  Uint8 previousB = 0; // 旧蓝色通道
  Uint8 previousA = 255; // 旧 alpha 通道
  SDL_GetRenderDrawColor(renderer, &previousR, &previousG, &previousB, &previousA); // 读取旧色

  // 打开 alpha 混合：粒子按寿命淡出，必须与背景半透明叠加
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // 视口尺寸用于屏幕外裁剪：粒子是短命小矩形，出屏后无需再提交绘制
  const float viewWidth = camera.viewportWidth();  // 视口宽（像素）
  const float viewHeight = camera.viewportHeight(); // 视口高（像素）

  // 逐粒绘制：固定数组扫描，活跃者换算到屏幕并按寿命比例淡出
  for (const Particle& particle : m_particles)
  {
    // 空闲槽位跳过
    if (!particle.active)
    {
      continue; // 下一槽位
    }

    // 世界坐标 -> 屏幕坐标：走相机渲染路径（震屏偏移在这里随世界一起抖动）
    const Vec2 screen = camera.worldToScreen(particle.x, particle.y);

    // 屏幕外裁剪（含粒子半径余量）：完全在视口外的粒子不提交绘制
    if (screen.x + particle.size < 0.0f || screen.x - particle.size > viewWidth ||
        screen.y + particle.size < 0.0f || screen.y - particle.size > viewHeight)
    {
      continue; // 出屏：跳过本粒子（不改变其状态，回到视口内仍可见）
    }

    // 寿命比例 [0,1]：1 = 刚发射，0 = 即将消失；直接作为 alpha 的乘数
    const float ratio = std::clamp(particle.life / particle.maxLife, 0.0f, 1.0f);
    // 最终 alpha = 基础不透明度 x 寿命比例（线性淡出，观感简单可预期）
    const Uint8 alpha = static_cast<Uint8>(static_cast<float>(particle.a) * ratio);

    // 设置本粒子的绘制色（颜色恒定、alpha 随寿命衰减）
    SDL_SetRenderDrawColor(renderer, particle.r, particle.g, particle.b, alpha);

    // 绘制矩形：以粒子位置为中心、size 为边长（屏幕空间，无纹理开销）
    const SDL_FRect destination{
      screen.x - particle.size * 0.5f, // 左边界 = 中心 - 半边长
      screen.y - particle.size * 0.5f, // 上边界 = 中心 - 半边长
      particle.size,                   // 宽 = 边长
      particle.size                    // 高 = 边长（正方形粒子）
    };
    SDL_RenderFillRect(renderer, &destination); // 填充矩形（当前绘制色 + BLEND）
  }

  // 恢复调用前的绘制颜色：不污染后续实体/HUD 绘制
  SDL_SetRenderDrawColor(renderer, previousR, previousG, previousB, previousA);
  // 恢复调用前的混合模式：不污染后续绘制
  SDL_SetRenderDrawBlendMode(renderer, previousBlend);
}

// 清空粒子池：全部槽位置空闲、发射游标归零（重开/切场景的确定性起点）
void ParticleSystem::clear()
{
  // 逐槽位只清 active：其余字段保留无妨（下次发射会全部覆写），
  // 但用默认 Particle 整体覆盖更直观且代价可忽略（512 x 小结构）
  for (Particle& particle : m_particles)
  {
    particle = Particle{}; // 恢复默认值（active = false）
  }
  m_nextIndex = 0; // 游标归零：下一次发射从池头开始
}

// 统计活跃粒子数：固定数组扫描（512 次 bool 判断，调用频率低）
int ParticleSystem::aliveCount() const
{
  int count = 0; // 活跃计数
  for (const Particle& particle : m_particles)
  {
    // 仅活跃槽位计数（空闲槽位不计）
    if (particle.active)
    {
      ++count; // 计数 +1
    }
  }
  return count; // 交还活跃总数
}
