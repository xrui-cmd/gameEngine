#include "engine/Input.h" // Input：本类声明（含 Action 结构与成员表）
#include <SDL3/SDL.h>     // SDL_GetKeyboardState / SDL_LogWarn / SDL_LOG_CATEGORY_APPLICATION
#include <algorithm>      // std::copy：整段拷贝本次键盘采样到基准数组

// 绑定/改键：把动作名映射到物理按键。同动作覆盖（改键）、新动作追加；
// 非法输入（空动作名 / 越界按键码）警告后忽略，绝不写入坏条目
void Input::bind(const char* action, SDL_Scancode key)
{
  // 空动作名永远无法被查询命中，且后续字符串比较会解引用空指针：警告并忽略
  if (action == nullptr || action[0] == '\0')
  {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "bind 收到空动作名，已忽略本次绑定");
    return; // 不产生任何动作条目，既有绑定不受影响
  }

  // 越界扫描码会在采样时越界访问状态数组（合法键位 0 ~ SDL_SCANCODE_COUNT-1）：
  // 拒绝并警告。正常调用传 SDL_SCANCODE_* 常量必然合法；此校验防的是
  // 上游透传的任意整数（如未来配置解析的结果），用警告代替崩溃
  if (static_cast<int>(key) < 0 || static_cast<int>(key) >= SDL_SCANCODE_COUNT)
  {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "bind 收到越界按键码 %d（动作 %s），已忽略本次绑定",
                static_cast<int>(key), action);
    return; // 拒绝坏数据，绑定表保持原样
  }

  // 先在既有条目中找同名动作：命中即就地改键（旧键立即失效、新键生效）
  for (Action& entry : m_actions)
  {
    if (entry.name == action)
    {
      entry.key = key; // 覆盖旧键；状态字段保持，下一帧采样按新键自然推进
      return;          // 改键完成，不追加新条目
    }
  }

  // 新动作：追加一条绑定（held/pressed/released 用默认初值 false 起步）
  m_actions.push_back(Action{ std::string(action), key });
}

// 查询：是否按住（持续状态；未绑定动作恒 false）
bool Input::held(const char* action) const
{
  const Action* entry = findAction(action); // 查表：未知动作得到空指针
  return entry != nullptr && entry->heldNow; // 空指针短路：未知动作直接 false
}

// 查询：是否刚按下（上升沿；未绑定动作恒 false）
bool Input::pressed(const char* action) const
{
  const Action* entry = findAction(action); // 同 held 的查表流程
  return entry != nullptr && entry->pressedNow; // 未知动作恒 false
}

// 查询：是否刚松开（下降沿；未绑定动作恒 false）
bool Input::released(const char* action) const
{
  const Action* entry = findAction(action); // 同 held 的查表流程
  return entry != nullptr && entry->releasedNow; // 未知动作恒 false
}

// 采样 SDL 键盘状态并推进边沿：宿主每个渲染帧调用一次
void Input::update()
{
  // 取得 SDL 内部键盘状态数组（事件泵已在帧首统一刷新；传 nullptr 表示
  // 不需要返回数组长度）。即时使用，不缓存、不释放
  const bool* keys = SDL_GetKeyboardState(nullptr);

  // 递给统一入口：nullptr（SDL 未初始化等极端情形）由它按「无按键」兜底
  updateFromKeyArray(keys);
}

// 用外部键盘状态数组推进边沿：单元测试注入合成状态的入口
void Input::updateFromKeyArray(const bool* keyboard)
{
  // 空指针是调用方错误：警告后按「所有键都没按」处理（不崩）。
  // 静态数组由零初始化保证全 false，只构造一次；与 update 的兜底语义一致
  if (keyboard == nullptr)
  {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "键盘状态数组为空指针，按「无按键」处理");
    static const bool allUp[SDL_SCANCODE_COUNT] = {}; // 全 false 的静态基准
    advanceEdges(allUp); // 走同一条推进路径，行为与正常采样一致
    return;
  }

  advanceEdges(keyboard); // 正常路径：直接推进边沿
}

// 边沿推进的公共实现：update 与 updateFromKeyArray 共用，保证行为一致
void Input::advanceEdges(const bool* keyboard)
{
  // 逐动作更新三种查询状态（条目少，线性遍历成本可忽略）
  for (Action& entry : m_actions)
  {
    const bool isDown = keyboard[entry.key]; // 本次采样：该动作绑定的键是否按住

    entry.heldNow = isDown; // held：直接来自本次采样（按住与否只关心当前）

    // pressed：本次按住 且 上次没按（真实的「没按 -> 按」过渡）。
    // 边沿以物理按键的采样历史为准：改键后新键原本就按着时，不会凭空
    // 产生一次 pressed——只有真实的按下瞬间才触发
    entry.pressedNow = isDown && !m_previousKeys[entry.key];

    // released：本次没按 且 上次按着（真实的「按 -> 松开」过渡）
    entry.releasedNow = !isDown && m_previousKeys[entry.key];
  }

  // 本次采样存为下一帧的比较基准。整段拷贝 512 个键位（连续内存，成本
  // 可忽略）；std::copy 比手写循环更不易错
  std::copy(keyboard, keyboard + SDL_SCANCODE_COUNT, m_previousKeys);
}

// 按名查找动作条目：未找到（含空指针/空串）返回 nullptr，查询方据此
// 统一返回 false。线性查找即可：动作条目少，成本是几次字符串比较
const Input::Action* Input::findAction(const char* action) const
{
  // 空指针/空串不可能命中任何条目（bind 已拒绝空名）：直接判定为未知
  // 动作，同时避免对空指针做字符串比较（解引用会崩溃）
  if (action == nullptr || action[0] == '\0')
  {
    return nullptr; // 未知动作，查询方返回 false
  }

  for (const Action& entry : m_actions)
  {
    if (entry.name == action) // std::string 与 C 字符串逐字符比较
    {
      return &entry; // 命中：返回条目地址，供查询方法读取状态字段
    }
  }

  return nullptr; // 未绑定：查询方按「恒 false」处理
}
