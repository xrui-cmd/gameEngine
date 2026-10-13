#ifndef __FONT_H__
#define __FONT_H__

#include <SDL3/SDL.h> // SDL_Renderer / SDL_FPoint / SDL_Color：绘制与测量接口的基础类型
#include <string>     // std::string：字体文件的路径参数

// ============================================================
// Font：TTF 字体资源与文本绘制的 RAII 封装（引擎层）
// ============================================================
//
// 背景：UI 文本原先用 SDL 内置调试字体（SDL_RenderDebugText），它只有
// ASCII 8x8 点阵，无法显示中文。本类基于 SDL3_ttf 提供中英文文本的
// 加载、测量与绘制能力，作为 UI 文本（菜单/HUD/叠层）的统一入口；
// 调试字体仅保留 FPS 等调试行使用（见 DebugText.h）。
//
// 职责与契约：
//   1. 资源所有权：本类拥有 TTF_Font，析构自动释放（RAII）；
//      destroy() 幂等可重入，配合场景提前释放也不会双重销毁
//   2. 失败降级：load() 失败返回 false 并警告一次；之后所有绘制/测量
//      调用安全跳过（不崩、不输出乱码残影），游戏照常可玩
//   3. 坐标语义：与 DebugText 一致，draw 的 (x, y) 是屏幕像素坐标系里
//      的「文字左上角」；颜色取渲染器当前绘制颜色（调用方先用
//      SDL_SetRenderDrawColor 设置），因此调用风格与 DebugText 相同
//
// 使用注意：
//   - 一个 Font 实例对应一个字号；不同字号（如标题 vs HUD）请各持一个实例
//   - TTF_Init 由应用外壳（GameDev）负责，使用前需保证已初始化
// ============================================================

// 前置声明：头文件里 TTF_Font 只以指针形式出现，不需要完整定义；
// 完整定义（typedef struct TTF_Font TTF_Font）由 Font.cpp 包含 SDL_ttf.h 获得
struct TTF_Font;

class Font
{
public:
    // 默认构造：空字体对象（未加载），可用 load 装载字体文件
    Font() = default;

    // 析构兜底：自动调用 destroy() 释放 TTF_Font，
    // 即使调用方忘记显式释放也不会泄漏
    ~Font();

    // 禁止拷贝：持有独占的 TTF_Font 资源，浅拷贝会造成双重释放。
    // 需要共享字体时应传递本对象的指针/引用（如场景持有成员、按引用传给绘制代码）
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // 加载字体文件并创建指定点数的字体对象。返回是否成功：
    //   成功 -> true；失败 -> false（已记录警告，调用方降级处理）
    // 幂等：重复 load 会先释放旧字体再装载新字体（不泄漏旧资源）
    bool load(const std::string& path, float ptSize);

    // 立即释放字体资源；幂等（重复调用安全），供场景提前释放或析构兜底使用
    void destroy();

    // 字体是否已就绪（load 成功后为 true）
    bool isLoaded() const { return m_font != nullptr; }

    // 绘制单行 UTF-8 文本：(x, y) 为文字左上角的屏幕像素坐标。
    // 内部流程：TTF_RenderText_Blended 光栅化 -> 创建纹理 -> 绘制 -> 立即销毁。
    // 当前 UI 文本规模很小，每帧重建纹理的开销可接受；
    // 后续若出现大量/长文本需求，可在此处加「文本 -> 纹理」缓存优化。
    // 返回是否成功绘制（字体未加载/空文本等场景返回 false，静默跳过）
    bool draw(SDL_Renderer* renderer, const char* utf8, float x, float y) const;

    // 水平居中版：centerX 为文字水平中心，y 为文字顶边（屏幕像素坐标）。
    // 宽度由 measure() 实测（中英混排宽度不定，不能按字符数估算）
    bool drawCentered(SDL_Renderer* renderer, const char* utf8, float centerX, float y) const;

    // 测量单行 UTF-8 文本的像素尺寸（宽, 高）。
    // 字体未加载或文本为空时返回 {0, 0}——调用方据此退化布局或跳过绘制
    SDL_FPoint measure(const char* utf8) const;

private:
    TTF_Font* m_font = nullptr; // 字体对象；load 成功后非空，destroy 后置回空
};

#endif // !__FONT_H__
