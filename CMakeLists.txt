cmake_minimum_required(VERSION 3.20) # 最低 CMake 3.20：保证 cxx_std_20 与 MSVC 生成器特性完整

# 声明引擎静态库工程；引擎实现均为 C++，无需启用 C 语言
project(GameEngine LANGUAGES CXX)

# ---------- 宿主前置检查 ----------
# 校验宿主是否已在引入本引擎前提供 SDL3 主库目标：
# 本引擎不内嵌 SDL 源码，四个 SDL 组件一律由宿主工程先行提供，避免同一 SDL 重复构建
if(NOT TARGET SDL3::SDL3) # 若 SDL3::SDL3 目标不存在（宿主漏配或引擎被独立拉起）
  # 配置期直接致命报错并给出修复指引，避免拖到链接阶段才出现费解的符号缺失
  message(FATAL_ERROR "未检测到 SDL3::SDL3 目标：宿主工程需先提供四个 SDL 组件（SDL3::SDL3、SDL3_image::SDL3_image、SDL3_ttf::SDL3_ttf、SDL3_mixer::SDL3_mixer）再 add_subdirectory 本引擎。引擎不内嵌 SDL，避免与宿主重复定义。")
endif() # 宿主前置检查结束

# ---------- 引擎静态库目标 ----------
# 定义静态库 target；收录引擎实现层 6 个 cpp，产出供宿主静态链接的 GameEngine.lib
add_library(GameEngine STATIC
  src/SceneManager.cpp # 场景管理器：延迟切换、帧末结算与事件/更新/渲染转发
  src/Font.cpp # 字体系统：TTF 字体加载与中文文本渲染
  src/Entity.cpp # 实体基类：纹理、位置与帧动画的统一实现
  src/Config.cpp # 配置解析：config/game.ini 读取与内置默认值回退
  src/Input.cpp # 输入系统：动作映射、改键绑定与边沿检测的采样实现
  src/AudioSystem.cpp # 音效系统：SDL3_mixer 的初始化、加载与播放封装
) # 静态库源文件清单结束

# 挂载 include 目录为 PUBLIC：宿主与引擎内部统一用 #include "engine/xxx.h" 的写法
target_include_directories(GameEngine PUBLIC include) # PUBLIC：随 target 传递给宿主，宿主无需重复添加

# 统一 C++20 标准（插值渲染等实现依赖）；PUBLIC 保证宿主以同一标准编译引擎头文件
target_compile_features(GameEngine PUBLIC cxx_std_20) # CMake 自动映射为 /std:c++20 或 -std=c++20

# MSVC 专属编译开关：源码含中文注释与字符串，强制按 UTF-8 读写
if(MSVC) # 仅微软编译器需要；GCC/Clang 默认按 UTF-8 处理源码
  # /utf-8：源码字符集与执行字符集均按 UTF-8，消除 C4819 等中文乱码类警告
  target_compile_options(GameEngine PRIVATE /utf-8)
endif() # MSVC 编译开关结束

# 链接 SDL 四件套：主库 PUBLIC（引擎公开头文件暴露其类型，必须传递给宿主）；
# 三个扩展库 PRIVATE（仅引擎 .cpp 内部使用，不向宿主传播）
target_link_libraries(GameEngine
  PUBLIC SDL3::SDL3 # SDL3 主库：窗口、渲染器、事件与基础类型
  PRIVATE SDL3_image::SDL3_image # 图片解码：IMG_LoadTexture 加载 PNG 纹理
  PRIVATE SDL3_ttf::SDL3_ttf # 文字渲染：TTF_OpenFont / TTF_RenderText_Blended
  PRIVATE SDL3_mixer::SDL3_mixer # 音效播放：MIX_LoadAudio / MIX_PlayTrack
) # 链接依赖清单结束
