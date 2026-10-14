#ifndef __VEC2_H__
#define __VEC2_H__

#include <cmath> // std::sqrt：length / normalized / distance 的平方根运算

// ============================================================
// Vec2：二维向量（引擎数学层，header-only）
// ============================================================
//
// 为什么需要它？
//   现状里「一对浮点」——位置 (x, y)、速度 (vx, vy)、位移、方向——每处
//   都手写两份表达式（x 一份、y 一份），计算稍长就容易只改一半（改 x
//   忘了 y）；传给函数也要拆成两个参数。Vec2 把「一对浮点」收成一个
//   类型：运算定义一次、处处复用，后续敌人追踪、击退位移、宝石飞向
//   玩家等 2D 玩法都会直接受益。
//
// 2D 向量用途一览：
//   - 位置 / 速度 / 位移增量的统一表示；
//   - 方向与距离：两点差 -> normalized() 得朝向，length() 得距离；
//   - 判定与混合：dot() 判方向关系，lerp() 做平滑过渡（渲染插值同款）。
//
// 3D 兼容姿势（架构红线①：数学层 header-only、渐进迁移）：
//   - 本文件位于 include/engine/ 的 math/ 子目录，header-only（无 .cpp、
//     无链接负担）；
//   - 接口按「通用向量的 2D 特化」设计：点积 / 长度 / 归一化 / 线性插值
//     这些概念对 3D 完全同构。将来 Vec3、Mat4 到来时沿用同一命名与风格
//     （只多一个 z 分量），2D 代码不需要为 3D 让路重写；
//   - 自由函数（dot / distance / lerp）与成员函数（length / normalized）
//     的分工也是为 3D 预备：对称的二元运算写成自由函数，作用于自身的
//     属性查询写成成员函数；
//   - 命名空间跟随引擎既有全局风格（与 UpdateContext / Config 等一致，
//     暂不引入 namespace），类型名本身承担辨识度。
//
// 坐标系约定：沿用引擎 2D 约定（原点在左上、y 轴向下、单位像素）。
// 数学运算本身与坐标系无关，故此处不再重复声明。
//
// 约定与边界行为：
//   - normalized() 对零向量返回 (0,0)：零向量没有方向可言，返回零向量
//     是明确、可预期的结果，调用方无需特判、也不会得到 NaN；
//   - 标量除法不设零保护：与 float 语义一致（除零得 inf/nan），调用方
//     保证除数非零（引擎内部调用点均为常量或已校验的正数）。
//
// constexpr 说明：
//   纯算术运算（构造 / 加减 / 标量乘除 / 点积 / 长度平方 / 插值）全部
//   标注 constexpr，可在编译期求值；length / normalized / distance 依赖
//   std::sqrt，C++20 的 std::sqrt 尚不是 constexpr 函数（C++26 起才允许），
//   故这三个函数暂不标注——不影响运行时使用。
// ============================================================

struct Vec2
{
  float x = 0.0f; // 横向分量（2D 下即屏幕 x 坐标方向的分量）
  float y = 0.0f; // 纵向分量（2D 下即屏幕 y 坐标方向的分量）

  // 默认构造：零向量 (0,0)。与成员默认值一致——实体初始位置等场景直接可用
  constexpr Vec2() = default;

  // 双分量构造：位置 / 速度等一切「一对浮点」的直接写法
  constexpr Vec2(float xValue, float yValue) : x(xValue), y(yValue)
  {
  }

  // 复合加法：v += other —— 位置累加位移（速度 × 时间步长的结果）的标准写法
  constexpr Vec2& operator+=(const Vec2& rhs)
  {
    x += rhs.x; // 横向分量各自相加
    y += rhs.y; // 纵向分量各自相加
    return *this; // 返回自身引用：支持链式写法与「就地修改」语义
  }

  // 复合减法：v -= other —— 逆向位移 / 坐标修正
  constexpr Vec2& operator-=(const Vec2& rhs)
  {
    x -= rhs.x; // 横向分量各自相减
    y -= rhs.y; // 纵向分量各自相减
    return *this; // 返回自身引用，语义同 operator+=
  }

  // 复合标量乘：v *= s —— 「速度向量整体按比例缩放」等场景
  constexpr Vec2& operator*=(float scalar)
  {
    x *= scalar; // 横向分量按标量缩放
    y *= scalar; // 纵向分量按标量缩放
    return *this; // 返回自身引用，语义同 operator+=
  }

  // 复合标量除：v /= s —— 与 *= 对称（如「位移按段数均分」）
  constexpr Vec2& operator/=(float scalar)
  {
    x /= scalar; // 横向分量除以标量（除零不设防，见文件头约定）
    y /= scalar; // 纵向分量除以标量
    return *this; // 返回自身引用，语义同 operator+=
  }

  // 长度平方：x² + y²（勾股定理的平方形式），省去开方。
  // 只做「比较谁更远 / 是否在半径内」时首选：比较距离时开方是纯浪费
  constexpr float lengthSquared() const
  {
    return x * x + y * y; // 两分量平方和，即与原点距离的平方
  }

  // 向量长度：√(x² + y²)。依赖 std::sqrt，故不标注 constexpr（见文件头说明）
  float length() const
  {
    return std::sqrt(lengthSquared()); // 平方和开方 = 欧几里得长度
  }

  // 归一化：返回同方向、长度为 1 的单位向量；不修改自身（按值返回新向量）
  Vec2 normalized() const
  {
    const float len = length(); // 先求当前长度（开方一次，两个分量共用）
    if (len == 0.0f)
    {
      return Vec2{}; // 零向量保护：没有方向可言，按约定返回 (0,0)
    }
    return Vec2{ x / len, y / len }; // 两个分量同除长度，得到单位向量
  }
};

// ---------- 二元运算（按值返回新向量，不修改操作数） ----------

// 向量 + 向量：位置加偏移 / 同源位移合成
constexpr Vec2 operator+(const Vec2& a, const Vec2& b)
{
  return Vec2{ a.x + b.x, a.y + b.y }; // 分量各自相加得到新向量
}

// 向量 - 向量：两点之差 = 由 b 指向 a 的位移向量（方向的来源）
constexpr Vec2 operator-(const Vec2& a, const Vec2& b)
{
  return Vec2{ a.x - b.x, a.y - b.y }; // 分量各自相减得到新向量
}

// 向量 × 标量：整体缩放（速度 × 时间步长 = 本步位移）
constexpr Vec2 operator*(const Vec2& v, float scalar)
{
  return Vec2{ v.x * scalar, v.y * scalar }; // 两个分量同乘标量
}

// 标量 × 向量：与上一重载交换顺序，支持 2.0f * v 的自然写法
constexpr Vec2 operator*(float scalar, const Vec2& v)
{
  return v * scalar; // 复用上一重载：交换乘法顺序，结果一致
}

// 向量 ÷ 标量：均匀缩小（除零不设防，与 float 语义一致）
constexpr Vec2 operator/(const Vec2& v, float scalar)
{
  return Vec2{ v.x / scalar, v.y / scalar }; // 两个分量同除标量
}

// ---------- 自由函数（与未来 Vec3 同构的通用向量操作） ----------

// 点积：a·b = ax*bx + ay*by。几何含义 = |a||b|cosθ，典型用途：
//   - 符号判断方向关系（>0 同向、<0 反向、=0 垂直）；
//   - 投影长度换算（把位移投影到某方向上）。
// 概念在 3D 完全同构：Vec3 到来时此函数只是多一项 z*z
constexpr float dot(const Vec2& a, const Vec2& b)
{
  return a.x * b.x + a.y * b.y; // 对应分量乘积之和
}

// 两点距离：|a - b|。两个位置直接用，无需手写差值再量长度
float distance(const Vec2& a, const Vec2& b)
{
  return (a - b).length(); // 差向量的长度即两点间距离
}

// 线性插值：t=0 返回 a，t=1 返回 b，中间按比例混合（如 t=0.5 取中点）。
// 渲染帧插值（alpha）、平滑跟随、动画过渡的通用工具；
// 不钳制 t 范围（外插也允许，t>1 得到延长线上的点），需要钳制由调用方处理
constexpr Vec2 lerp(const Vec2& a, const Vec2& b, float t)
{
  return Vec2{ a.x + (b.x - a.x) * t, // 横向：起点 + 差值 × 进度
               a.y + (b.y - a.y) * t }; // 纵向：同式
}

#endif // !__VEC2_H__
