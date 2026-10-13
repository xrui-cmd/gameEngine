#include "engine/Font.h"
#include <SDL3_ttf/SDL_ttf.h> // TTF_OpenFont / TTF_CloseFont / TTF_RenderText_Blended / TTF_GetStringSize

// ============================================================
// Font 的实现：RAII 生命周期 + 文本度量/绘制。
// 头文件已说明设计背景，这里只补充实现层面的关键决策：
//   - destroy() 幂等：TTF_CloseFont 只对非空指针调用，置空后重复调用成空操作
//   - draw() 每帧「光栅化->转纹理->绘制->销毁」：UI 文本短小，
//     开销可接受；缓存优化留给后续需要时加（见头文件注释）
// ============================================================

Font::~Font()
{
    // 析构兜底释放：即使调用方从未显式 destroy，字体资源也不会泄漏
    destroy();
}

void Font::destroy()
{
    // 只有字体确实存在时才释放，避免对空指针调用 TTF_CloseFont；
    // 释放后置空，使本函数天然幂等（可重复调用、可提前调用）
    if (m_font != nullptr)
    {
        TTF_CloseFont(m_font); // 释放字体对象（含字形缓存等内部资源）
        m_font = nullptr;      // 置空：防止二次释放（析构兜底再调用也安全）
    }
}

bool Font::load(const std::string& path, float ptSize)
{
    // 幂等：先释放可能存在的旧字体，保证重复 load 不会泄漏旧资源
    destroy();

    // TTF_OpenFont：打开 TTF/OTF 字体文件并创建指定字号的字体对象。
    // ptSize 是字号（像素高度语义），中文 UI 建议 24 起步才清晰可读
    m_font = TTF_OpenFont(path.c_str(), ptSize);

    // 打开失败（文件缺失/损坏/内存不足）：记录一次警告并返回失败。
    // 这是「字体缺失不崩游戏」的入口——之后绘制/测量都会安全跳过
    if (m_font == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "打开字体失败（%s）：%s", path.c_str(), SDL_GetError());
        return false;
    }

    // 装载成功：字体就绪，后续 draw/measure 均可正常工作
    return true;
}

SDL_FPoint Font::measure(const char* utf8) const
{
    // 字体未加载或文本为空：返回零尺寸。
    // 调用方（如居中绘制）据此安全跳过，不会拿垃圾数值去算布局
    if (m_font == nullptr || utf8 == nullptr || utf8[0] == '\0')
    {
        return SDL_FPoint{ 0.0f, 0.0f };
    }

    int width = 0;  // 文本宽（像素），由 TTF_GetStringSize 回填
    int height = 0; // 文本高（像素），同上

    // TTF_GetStringSize：按字体度量计算文本尺寸（不做实际光栅化，开销小）。
    // 长度为 0 表示按 '\0' 结尾读取 UTF-8 串
    if (!TTF_GetStringSize(m_font, utf8, 0, &width, &height))
    {
        // 度量失败（非法字串等）：与「未加载」同策略，返回零尺寸
        return SDL_FPoint{ 0.0f, 0.0f };
    }

    // 整数像素尺寸转浮点：与布局坐标统一量纲
    return SDL_FPoint{ static_cast<float>(width), static_cast<float>(height) };
}

bool Font::draw(SDL_Renderer* renderer, const char* utf8, float x, float y) const
{
    // 字体未加载（load 失败/未调用）或参数非法：静默安全跳过。
    // 「警告一次」发生在 load 阶段，这里不再重复记录，避免每帧刷屏
    if (m_font == nullptr || renderer == nullptr || utf8 == nullptr || utf8[0] == '\0')
    {
        return false;
    }

    // 取渲染器当前绘制颜色作为文字颜色：与 DebugText 的调用约定一致，
    // 调用方先用 SDL_SetRenderDrawColor 设色，再直接 draw，无需额外传色
    Uint8 r = 255; // 红分量初值（255=白）：读取失败时按白色兜底
    Uint8 g = 255; // 绿分量初值（255=白），同上
    Uint8 b = 255; // 蓝分量初值（255=白），同上
    Uint8 a = 255; // 透明度初值（255=不透明），同上
    SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
    const SDL_Color color{ r, g, b, a }; // SDL_ttf 渲染接口需要的颜色结构

    // 光栅化：把 UTF-8 文本按当前字体画成带抗锯齿（Blended）的像素表面。
    // 长度为 0 表示按 '\0' 结尾读取；每帧重建的开销说明见头文件
    SDL_Surface* surface = TTF_RenderText_Blended(m_font, utf8, 0, color);
    if (surface == nullptr)
    {
        // 渲染失败（内存不足等）通常为环境级异常：只警告第一次，避免每帧刷屏
        static bool s_rasterizeErrorLogged = false; // 光栅化失败是否已记录（进程内一次）
        if (!s_rasterizeErrorLogged)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "文本光栅化失败：%s", SDL_GetError());
            s_rasterizeErrorLogged = true; // 后续失败不再重复记录
        }
        return false;
    }

    // 先记住表面尺寸：转纹理后表面会被立即释放，尺寸数据不能事后查
    const float texW = static_cast<float>(surface->w); // 文本纹理宽（像素）
    const float texH = static_cast<float>(surface->h); // 文本纹理高（像素）

    // 表面 -> GPU 纹理：一次性上传像素数据
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

    // CPU 侧表面使命完成：无论转纹理成功与否都立即释放（避免泄漏）
    SDL_DestroySurface(surface);

    // 转纹理失败同样是环境级异常：沿用「只记一次」策略
    if (texture == nullptr)
    {
        static bool s_textureErrorLogged = false; // 转纹理失败是否已记录（进程内一次）
        if (!s_textureErrorLogged)
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "文本纹理创建失败：%s", SDL_GetError());
            s_textureErrorLogged = true; // 后续失败不再重复记录
        }
        return false;
    }

    // 目标矩形：以调用方给的 (x, y) 为左上角，尺寸取纹理实际大小。
    // 不做缩放——字号由 load 时的 ptSize 决定，排版数值更直观
    const SDL_FRect dstRect{ x, y, texW, texH };

    // 把文本纹理画到渲染目标（源矩形传 nullptr = 整张纹理）
    const bool drawn = SDL_RenderTexture(renderer, texture, nullptr, &dstRect);

    // 纹理使命完成：立即销毁，显存不跨帧占用（代价是每帧重建，见头文件说明）
    SDL_DestroyTexture(texture);

    // 把绘制结果如实回传给调用方（失败通常是 SDL 渲染目标的问题）
    return drawn;
}

bool Font::drawCentered(SDL_Renderer* renderer, const char* utf8, float centerX, float y) const
{
    // 先实测文本宽度：中英混排宽度不定，不能按字符数估算
    const SDL_FPoint size = measure(utf8);

    // 宽度为 0（字体未加载/空文本）：与 draw 同策略，安全跳过
    if (size.x <= 0.0f)
    {
        return false;
    }

    // 由「水平中心」反推左上角 x：centerX - 文本宽 / 2，再复用 draw 完成绘制
    return draw(renderer, utf8, centerX - size.x / 2.0f, y);
}
