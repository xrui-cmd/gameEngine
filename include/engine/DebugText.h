#ifndef __DEBUGTEXT_H__
#define __DEBUGTEXT_H__

#include <SDL3/SDL.h> // SDL_RenderDebugText / SDL_SetRenderScale / SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
#include <cstdarg>    // va_list / va_start / va_end：drawFormat 的可变参数处理

// ============================================================
// DebugText：SDL_RenderDebugText 系列的便捷封装（header-only）
// ============================================================
//
// 背景：HUD 的调试文字每次绘制都要重复同一套样板——
//   开缩放 -> 按逻辑坐标绘制 -> 恢复缩放。
// 本类把样板收进一处，并统一两件事：
//
//   1. 坐标约定：调用方传入「屏幕像素坐标」（渲染输出尺寸的坐标系），
//      类内部负责除以缩放倍数换算成逻辑坐标。屏幕像素是直觉单位，
//      以后调整缩放倍数时，各处的文字位置不用跟着改。
//   2. 字符宽度：SDL 调试字体恒为 SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
//      （8）逻辑像素/字符，水平居中所需的文字宽度由此精确算出。
//
// 使用注意：
//   - 颜色由调用方在调用前用 SDL_SetRenderDrawColor 设置
//     （SDL_RenderDebugText 使用当前绘制颜色）
//   - 每次调用结束都恢复默认缩放 (1,1)，不会影响之后的世界绘制
//   - 调试字体只支持 ASCII，本类不处理中文排版
// ============================================================

class DebugText
{
public:
    // 绘制单行文字。scale 为放大倍数；(x, y) 为文字左上角的屏幕像素坐标
    static void draw(SDL_Renderer* renderer, float scale, float x, float y, const char* text)
    {
        // 开启放大缩放：之后的绘制坐标按「逻辑空间」解释
        SDL_SetRenderScale(renderer, scale, scale);
        // 屏幕坐标 ÷ 缩放 = 逻辑坐标；文字本体 8x8 逻辑像素/字符
        SDL_RenderDebugText(renderer, x / scale, y / scale, text);
        // 恢复默认缩放：否则后续世界绘制也会被放大并错位
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
    }

    // 同 draw，但支持 printf 风格格式化（如 "Score: %d"）。
    // 先格式化到缓冲区再走 draw：C++ 无法可移植地直接转发可变参数
    static void drawFormat(SDL_Renderer* renderer, float scale, float x, float y, const char* fmt, ...)
    {
        char buffer[256]; // 教学项目文案都很短：256 字节足够容纳任何一行 HUD
        // 可变参数处理三件套：取得参数列表 -> 格式化 -> 结束
        va_list args;
        va_start(args, fmt);
        SDL_vsnprintf(buffer, sizeof(buffer), fmt, args); // SDL 自带的跨平台 vsnprintf
        va_end(args);
        draw(renderer, scale, x, y, buffer); // 格式化结果交给 draw 完成缩放与绘制
    }

    // 水平居中版：(centerX, y) = 文字水平中心 + 文字顶边的屏幕像素坐标。
    // 需要「居中 + 格式化」时，可先用 SDL_snprintf 格式化到缓冲区再传进来
    static void drawCentered(SDL_Renderer* renderer, float scale, float centerX, float y, const char* text)
    {
        // 文字宽度 = 字符数 × 每字符 8 逻辑像素 × 缩放倍数（换算到屏幕像素）
        const float textWidth = static_cast<float>(SDL_strlen(text)) *
                                static_cast<float>(SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE) * scale;
        // 由「水平中心」反推出左上角 x，再复用 draw 的统一缩放逻辑
        draw(renderer, scale, centerX - textWidth / 2.0f, y, text);
    }

    // 纯静态工具类：删除默认构造函数，禁止实例化
    DebugText() = delete;
};

#endif // !__DEBUGTEXT_H__
