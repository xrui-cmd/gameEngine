#include "engine/Input.h" // Input：本类声明（含 Action 结构与成员表）
#include <SDL3/SDL.h>     // SDL_GetKeyboardState / SDL_GetMouseState / SDL_LogWarn / SDL_LOG_CATEGORY_APPLICATION
#include <algorithm>      // std::copy：整段拷贝本次键盘采样到基准数组

// 绑定/改键：把动作名映射到物理按键。同动作覆盖（改键）、新动作追加；
// 非法输入（空动作名 / 越界按键码）警告后忽略，绝不写入坏条目。
// 本方法只改「键盘路」：动作已有的鼠标绑定（bindMouse）保持不动
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

  // 先在既有条目中找同名动作：命中即就地改键（旧键立即失效、新键生效），
  // 鼠标绑定字段原样保留（两路绑定互不干扰）
  for (Action& entry : m_actions)
  {
    if (entry.name == action)
    {
      entry.key = key; // 覆盖旧键；状态字段保持，下一帧采样按新键自然推进
      return;          // 改键完成，不追加新条目
    }
  }

  // 新动作：追加一条绑定（held/pressed/released 用默认初值 false 起步；
  // mouseButton 保持默认 0 = 暂无鼠标按钮绑定）
  m_actions.push_back(Action{ std::string(action), key });
}

// 绑定/改鼠标：把动作名映射到鼠标按钮。语义与 bind 同款：同动作覆盖
// （改鼠标绑定）、新动作追加；非法输入（空动作名 / 越界按钮号）警告后忽略。
// 本方法只改「鼠标路」：动作已有的键盘绑定保持不动
void Input::bindMouse(const char* action, Uint8 button)
{
  // 空动作名检查与 bind 同款：无法命中且会解引用空指针，警告并忽略
  if (action == nullptr || action[0] == '\0')
  {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "bindMouse 收到空动作名，已忽略本次绑定");
    return; // 不产生任何动作条目
  }

  // 越界按钮号：SDL 的按钮掩码是 32 位、从左键（1）到 X2（5）有命名常量。
  // 采样时按 SDL_BUTTON_MASK(button) 取位，0/超大值都会产生无意义掩码
  // （0 减 1 会整体移位溢出）：拒绝并警告，防上游透传任意整数
  if (button < SDL_BUTTON_LEFT || button > SDL_BUTTON_X2)
  {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "bindMouse 收到越界按钮号 %d（动作 %s），已忽略本次绑定",
                static_cast<int>(button), action);
    return; // 拒绝坏数据，绑定表保持原样
  }

  // 先在既有条目中找同名动作：命中即就地改鼠标绑定（键盘绑定保持不动）
  for (Action& entry : m_actions)
  {
    if (entry.name == action)
    {
      entry.mouseButton = button; // 覆盖旧鼠标按钮；键盘路不受影响
      return;                     // 改绑定完成，不追加新条目
    }
  }

  // 新动作（可以只有鼠标绑定、没有键盘键）：key 保持默认 UNKNOWN——
  // 采样时读键盘数组的第 0 位（无物理键映射到此位，恒 false，无副作用）
  m_actions.push_back(Action{ std::string(action), SDL_SCANCODE_UNKNOWN, button });
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

// 采样 SDL 键盘与鼠标状态并推进边沿：宿主每个渲染帧调用一次。
// 一次调用完成两路采样：键盘数组 + 鼠标状态（按钮掩码与指针位置），
// 键盘与鼠标共享同一份帧快照，边沿口径完全一致
void Input::update()
{
  // 取得 SDL 内部键盘状态数组（事件泵已在帧首统一刷新；传 nullptr 表示
  // 不需要返回数组长度）。即时使用，不缓存、不释放
  const bool* keys = SDL_GetKeyboardState(nullptr);

  // 鼠标状态：一次调用同时取「按钮掩码 + 指针位置」。位置是「相对有焦点
  // 窗口左上角」的客户区坐标（float 像素），与游戏世界坐标同一套约定
  float mouseX = 0.0f; // 指针 x（接收 SDL 写入）
  float mouseY = 0.0f; // 指针 y（接收 SDL 写入）
  const SDL_MouseButtonFlags buttons = SDL_GetMouseState(&mouseX, &mouseY);

  // 位置快照先落定再推进边沿：同一帧内所有固定步读到同一个位置
  m_mousePosition = Vec2{ mouseX, mouseY };

  // 递进统一入口：键盘空指针（SDL 未初始化等极端情形）由它按「无按键」兜底
  advanceEdges(keys, buttons);
}

// 用外部键盘状态数组推进边沿：单元测试注入合成状态的入口。
// 本注入口只推进键盘路——鼠标路视为「保持上一次采样」：
// 把上一次的按钮掩码当作本次传入，按钮状态「无变化」（按住延续、
// 不产生新边沿），指针位置保持不动。键盘用例因此不受鼠标状态干扰
void Input::updateFromKeyArray(const bool* keyboard)
{
  advanceEdges(keyboard, m_previousMouseButtons); // 鼠标路零推进，键盘路正常推进
}

// 用外部鼠标状态推进边沿：单元测试注入合成状态的入口。
// 本注入口只推进鼠标路——键盘路视为「保持上一次采样」：
// 把上一次的键盘数组当作本次传入，按键状态「无变化」。鼠标用例
// 因此不受键盘状态干扰；指针位置按传入坐标就地更新
void Input::updateFromMouseState(SDL_MouseButtonFlags buttons, float mouseX, float mouseY)
{
  m_mousePosition = Vec2{ mouseX, mouseY }; // 位置快照：与 update 同样即时落定
  advanceEdges(m_previousKeys, buttons);    // 键盘路零推进，鼠标路正常推进
}

// 边沿推进的公共实现：update 与两个注入入口共用，保证行为一致
void Input::advanceEdges(const bool* keyboard, SDL_MouseButtonFlags mouseButtons)
{
  // 空键盘数组是调用方错误：警告后按「所有键都没按」处理（不崩）。
  // 静态数组由零初始化保证全 false，只构造一次；与 update 的兜底语义一致
  if (keyboard == nullptr)
  {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "键盘状态数组为空指针，按「无按键」处理");
    static const bool allUp[SDL_SCANCODE_COUNT] = {}; // 全 false 的静态基准
    keyboard = allUp; // 后续统一按这个基准推进，行为与正常采样一致
  }

  // 逐动作更新三种查询状态（条目少，线性遍历成本可忽略）。
  // 键盘路与鼠标路分别判定再取「或」：任一路有边沿，动作就报告边沿
  for (Action& entry : m_actions)
  {
    // ---- 键盘路 ──
    // key 为 UNKNOWN（0）时读键位数组第 0 位：没有物理键映射此位，恒 false，
    // 「只有鼠标绑定」的动作因此天然无键盘状态，无需特判
    const bool keyDown = keyboard[entry.key];        // 本次采样：键盘键是否按住
    const bool wasKeyDown = m_previousKeys[entry.key]; // 上一次采样：键盘键是否按住

    // ---- 鼠标路 ──
    // mouseButton 为 0（无绑定）时掩码取 0，后续与运算恒 false；
    // 0 不能直接进 SDL_BUTTON_MASK（(0-1) 的移位溢出是未定义行为），
    // 所以先算掩码、三元短路保护
    const SDL_MouseButtonFlags mask =
      (entry.mouseButton != 0) ? SDL_BUTTON_MASK(entry.mouseButton) : 0u;
    const bool mouseDown = (mouseButtons & mask) != 0;             // 本次采样：鼠标按钮是否按住
    const bool wasMouseDown = (m_previousMouseButtons & mask) != 0; // 上一次采样：鼠标按钮是否按住

    // 合并两路：按住状态取或；边沿取或（任一路从没按变按下 = 动作刚按下；
    // 任一路从按着变松开 = 动作刚松开。两路都按着时松开一路不算动作松开）
    entry.heldNow = keyDown || mouseDown;

    entry.pressedNow = (keyDown && !wasKeyDown) || (mouseDown && !wasMouseDown);

    entry.releasedNow = (!keyDown && wasKeyDown) || (!mouseDown && wasMouseDown);
  }

  // 本次采样存为下一帧的比较基准。整段拷贝 512 个键位（连续内存，成本
  // 可忽略）；std::copy 比手写循环更不易错。鼠标掩码是单个整数直接赋值
  std::copy(keyboard, keyboard + SDL_SCANCODE_COUNT, m_previousKeys);
  m_previousMouseButtons = mouseButtons;
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
