#ifndef __INPUT_H__
#define __INPUT_H__

#include <SDL3/SDL_mouse.h>    // SDL_MouseButtonFlags / SDL_BUTTON_*：鼠标按钮掩码与按钮号
#include <SDL3/SDL_scancode.h> // SDL_Scancode / SDL_SCANCODE_COUNT：按键编码与状态数组长度
#include <string>              // std::string：动作名（如 "move_up"）的存储与比较
#include <vector>              // std::vector：动作映射表（条目少，线性查找即可）
#include "engine/math/Vec2.h"  // Vec2：鼠标位置（窗口客户区坐标）的类型化表示

// ============================================================
// Input：输入系统（键盘 + 鼠标的动作映射与边沿检测）
// ============================================================
//
// 为什么需要它？
//   玩法代码直接读 ctx.keyboard[SDL_SCANCODE_W] 时，按键与玩法焊死：
//   想加改键、想区分「按住」与「刚按下」都得各处自己比对上一帧状态。
//   本类把「物理按键/鼠标按钮」映射成「语义动作」——玩法只问动作名
//   （如 "move_up"、"attack"），不关心它挂在哪个键上；改键只是重新 bind，
//   玩法代码一行不改。
//
// 两路输入（同一套查询）：
//   - 键盘：bind(action, key) 绑定物理扫描码（与键盘布局无关）；
//   - 鼠标：bindMouse(action, button) 绑定鼠标按钮（左/中/右键等），
//     并用 mousePosition() 查询指针位置（窗口客户区坐标）。
//   键盘与鼠标绑定互不影响：同一动作可同时拥有两路绑定，held/pressed/
//   released 查询把两路状态「或」在一起——查询方不关心动作挂在哪个设备上。
//
// 三个查询语义（同一动作）：
//   - held(action)     ：当前是否按住（持续状态）；
//   - pressed(action)  ：是否「刚按下」（上升沿：上一次采样没按、这次按下）；
//   - released(action) ：是否「刚松开」（下降沿：上一次采样按着、这次松开）。
//   任一绑定路出现边沿，动作就报告该边沿（例如键盘键与鼠标键同时按着，
//   松开其中一个不算动作 released——仍有路按着）。
//
// 采样语义（重要）：
//   update() 按「渲染帧」采样一次 SDL 键盘与鼠标状态（同一条采样路径：
//   一次调用同时取键盘数组、鼠标按钮掩码与指针位置），边沿以「两次
//   采样之间」的变化判定。同一渲染帧内可能补多个固定步（见宿主主循环），
//   全部固定步共享同一份快照——边沿在整帧内所有步都可见，不会出现
//   「第一个固定步消耗了按下事件、后面的步看不到」的丢事件问题。
//
// 容错约定（与引擎「资源问题不让游戏崩溃」一脉相承）：
//   - 查询未绑定/不存在的动作一律恒返回 false（不崩、不警告——玩法可能
//     查询尚未绑定的动作，属正常流程）；
//   - bind/bindMouse 收到空动作名 / 非法按键码时警告并忽略，防止比较与
//     索引崩溃；
//   - update() 在拿不到 SDL 键盘数组等极端情形下按「所有键都没按」处理
//     （原本按住的键视为松开，状态自然复位而非卡死）。
//
// 存储选型：动作条目少（个位数到十几个），std::vector + 线性查找即可；
// 不引入哈希表——省去哈希开销与心智负担，可读性优先。
//
// 线程约定：所有方法均在主线程（宿主帧循环）调用，无并发保护需求。
// ============================================================

class Input
{
public:
  // 绑定/改键：把动作名映射到物理按键（SDL 扫描码，与键盘布局无关，
  // AZERTY/QWERTY 下同一物理位置同码）。
  // 同一动作重复 bind = 改键：覆盖旧绑定，旧键立即失效；
  // 动作名建议「动词_宾语」小写下划线风格（如 "move_up"、"attack"）。
  // 本方法只改「键盘路」：动作已有的鼠标绑定（bindMouse）保持不动
  void bind(const char* action, SDL_Scancode key);

  // 绑定/改鼠标：把动作名映射到鼠标按钮（SDL_BUTTON_LEFT / MIDDLE / RIGHT
  // 等——1 起计的按钮号）。语义与 bind 同款：同动作重复调用 = 改鼠标绑定、
  // 空动作名/非法按钮号警告并忽略；本方法只改「鼠标路」，键盘绑定保持不动。
  // 首次为某动作调用会创建条目（该动作可以只有鼠标绑定、没有键盘键）
  void bindMouse(const char* action, Uint8 button);

  // 最近一次采样时鼠标指针的位置（窗口客户区坐标：左上原点、y 向下，
  // 单位与窗口逻辑尺寸一致）。从未采样过时为 (0,0)。
  // 注意：这是「采样快照」而不是实时查询——与 held/pressed 同一份时序，
  // 同一渲染帧的所有固定步读到同一个位置（插值平滑由渲染层另做）
  Vec2 mousePosition() const { return m_mousePosition; }

  // 查询动作是否正被按住（持续状态；未绑定动作恒 false）
  bool held(const char* action) const;

  // 查询动作是否「刚按下」（上升沿，仅维持到下一次采样；未绑定动作恒 false）
  bool pressed(const char* action) const;

  // 查询动作是否「刚松开」（下降沿，仅维持到下一次采样；未绑定动作恒 false）
  bool released(const char* action) const;

  // 采样 SDL 键盘与鼠标状态并推进边沿：每个渲染帧在装配 UpdateContext
  // 之前调用一次。一次调用完成两路采样：键盘数组（SDL_GetKeyboardState）
  // 与鼠标状态（SDL_GetMouseState：按钮掩码 + 指针位置）——键盘与鼠标
  // 共享同一份帧快照，边沿判定口径完全一致
  void update();

  // 用外部提供的键盘状态数组推进边沿（供单元测试注入合成状态——不依赖
  // 窗口与真实键盘）。数组长度约定为 SDL_SCANCODE_COUNT，与
  // SDL_GetKeyboardState 返回的数组同构；nullptr 按「全无按键」兜底。
  // 本注入口只推进键盘路：鼠标路视为「保持上一次采样」（按住状态延续、
  // 不产生新边沿），指针位置保持不动——键盘用例不受鼠标状态干扰
  void updateFromKeyArray(const bool* keyboard);

  // 用外部提供的鼠标状态推进边沿（供单元测试注入合成状态——不依赖窗口
  // 与真实鼠标）：buttons 是按钮掩码（SDL_BUTTON_MASK(SDL_BUTTON_LEFT)
  // 这类位值），mouseX/mouseY 是窗口客户区坐标。nullptr 概念不适用——
  // 全松手即传 0。本注入口只推进鼠标路：键盘路视为「保持上一次采样」，
  // 鼠标用例不受键盘状态干扰
  void updateFromMouseState(SDL_MouseButtonFlags buttons, float mouseX, float mouseY);

private:
  // 一条动作绑定：动作名 + 键盘键 + 鼠标按钮 + 最近一次采样得到的三种查询状态
  struct Action
  {
    std::string name;                      // 动作名（如 "move_up"；bind/bindMouse 保证非空）
    SDL_Scancode key = SDL_SCANCODE_UNKNOWN; // 键盘绑定；UNKNOWN = 该动作没有键盘键（无采样负担）
    Uint8 mouseButton = 0;                 // 鼠标按钮绑定；0 = 没有鼠标按钮（SDL_BUTTON_LEFT 起为正数）
    bool heldNow = false;                  // 最近采样：该动作任一路是否按住
    bool pressedNow = false;               // 最近采样：是否上升沿（由没按变按下的瞬间）
    bool releasedNow = false;              // 最近采样：是否下降沿（由按着变松开的瞬间）
  };

  // 按名查找动作条目；找不到（含空指针/空串）返回 nullptr，
  // 查询方法据此统一返回 false
  const Action* findAction(const char* action) const;

  // 边沿推进的公共实现（update / updateFromKeyArray / updateFromMouseState
  // 都汇入这里）：先按「本次采样 vs 上一次采样」更新每个动作的三种状态
  // （键盘路与鼠标路取合并），再把本次采样存为下一次比较的基准
  void advanceEdges(const bool* keyboard, SDL_MouseButtonFlags mouseButtons);

private:
  std::vector<Action> m_actions; // 动作映射表（线性查找：条目少，够用且直观）

  // 上一次采样的键盘状态（SDL_SCANCODE_COUNT 个键位，与 SDL 返回的数组同构）。
  // 边沿判定 = 「本次按键状态」×「上一次采样状态」；全 0 起步 = 首帧按下
  // 会正确报告为 pressed（从「没按」到「按下」的上升沿）
  bool m_previousKeys[SDL_SCANCODE_COUNT] = {};

  // 上一次采样的鼠标按钮掩码（与 SDL_GetMouseState 返回值同构）：
  // 鼠标路边沿判定的基准；0 = 全松手起步（首帧按下正确报告 pressed）
  SDL_MouseButtonFlags m_previousMouseButtons = 0;

  // 最近一次采样时鼠标指针的窗口客户区坐标（按值快照，见 mousePosition）。
  // 未采样过时为 (0,0)——与「鼠标位于客户区左上角」同值，但那是采样前
  // 的占位状态，玩法不应在 update 之前读取
  Vec2 m_mousePosition{};
};

#endif // !__INPUT_H__
