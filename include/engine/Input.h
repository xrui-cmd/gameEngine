#ifndef __INPUT_H__
#define __INPUT_H__

#include <SDL3/SDL_scancode.h> // SDL_Scancode / SDL_SCANCODE_COUNT：按键编码与状态数组长度
#include <string>              // std::string：动作名（如 "move_up"）的存储与比较
#include <vector>              // std::vector：动作映射表（条目少，线性查找即可）

// ============================================================
// Input：键盘输入系统（动作映射 + 边沿检测）
// ============================================================
//
// 为什么需要它？
//   玩法代码直接读 ctx.keyboard[SDL_SCANCODE_W] 时，按键与玩法焊死：
//   想加改键、想区分「按住」与「刚按下」都得各处自己比对上一帧状态。
//   本类把「物理按键」映射成「语义动作」——玩法只问动作名（如
//   "move_up"、"attack"），不关心它挂在哪个键上；改键只是重新 bind，
//   玩法代码一行不改。
//
// 三个查询语义（同一动作）：
//   - held(action)     ：当前是否按住（持续状态）；
//   - pressed(action)  ：是否「刚按下」（上升沿：上一次采样没按、这次按下）；
//   - released(action) ：是否「刚松开」（下降沿：上一次采样按着、这次松开）。
//
// 采样语义（重要）：
//   update() 按「渲染帧」采样一次 SDL 键盘状态，边沿以「两次采样之间」的
//   变化判定。同一渲染帧内可能补多个固定步（见宿主主循环），全部固定步
//   共享同一份快照——边沿在整帧内所有步都可见，不会出现「第一个固定步
//   消耗了按下事件、后面的步看不到」的丢事件问题。
//
// 容错约定（与引擎「资源问题不让游戏崩溃」一脉相承）：
//   - 查询未绑定/不存在的动作一律恒返回 false（不崩、不警告——玩法可能
//     查询尚未绑定的动作，属正常流程）；
//   - bind 收到空动作名 / 越界按键时警告并忽略，防止比较与索引崩溃；
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
  // 动作名建议「动词_宾语」小写下划线风格（如 "move_up"、"attack"）
  void bind(const char* action, SDL_Scancode key);

  // 查询动作是否正被按住（持续状态；未绑定动作恒 false）
  bool held(const char* action) const;

  // 查询动作是否「刚按下」（上升沿，仅维持到下一次采样；未绑定动作恒 false）
  bool pressed(const char* action) const;

  // 查询动作是否「刚松开」（下降沿，仅维持到下一次采样；未绑定动作恒 false）
  bool released(const char* action) const;

  // 采样 SDL 键盘状态并推进边沿：每个渲染帧在装配 UpdateContext 之前
  // 调用一次。内部与 updateFromKeyArray 走同一条推进路径，行为一致
  void update();

  // 用外部提供的键盘状态数组推进边沿（供单元测试注入合成状态——不依赖
  // 窗口与真实键盘）。数组长度约定为 SDL_SCANCODE_COUNT，与
  // SDL_GetKeyboardState 返回的数组同构；nullptr 按「全无按键」兜底
  void updateFromKeyArray(const bool* keyboard);

private:
  // 一条动作绑定：动作名 + 物理按键 + 最近一次采样得到的三种查询状态
  struct Action
  {
    std::string name;         // 动作名（如 "move_up"；bind 保证非空）
    SDL_Scancode key;         // 当前绑定的物理按键（可被再次 bind 覆盖）
    bool heldNow = false;     // 最近采样：该键是否按住
    bool pressedNow = false;  // 最近采样：是否上升沿（由没按变按下的瞬间）
    bool releasedNow = false; // 最近采样：是否下降沿（由按着变松开的瞬间）
  };

  // 按名查找动作条目；找不到（含空指针/空串）返回 nullptr，
  // 查询方法据此统一返回 false
  const Action* findAction(const char* action) const;

  // 边沿推进的公共实现（update 与 updateFromKeyArray 都汇入这里）：
  // 先按「本次采样 vs 上一次采样」更新每个动作的三种状态，
  // 再把本次采样存为下一次比较的基准
  void advanceEdges(const bool* keyboard);

private:
  std::vector<Action> m_actions; // 动作映射表（线性查找：条目少，够用且直观）

  // 上一次采样的键盘状态（SDL_SCANCODE_COUNT 个键位，与 SDL 返回的数组同构）。
  // 边沿判定 = 「本次按键状态」×「上一次采样状态」；全 0 起步 = 首帧按下
  // 会正确报告为 pressed（从「没按」到「按下」的上升沿）
  bool m_previousKeys[SDL_SCANCODE_COUNT] = {};
};

#endif // !__INPUT_H__
