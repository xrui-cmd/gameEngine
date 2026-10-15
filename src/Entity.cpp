#include "engine/Entity.h"
#include <SDL3_image/SDL_image.h> // IMG_LoadTexture：SDL3_image 提供的图片文件加载 API
#include <algorithm>              // std::clamp：动画子区间与帧号的防御性夹取
#include <string>                 // std::string：拼接素材文件的绝对路径

// 析构：兜底释放纹理。
// release() 幂等，即使 GameDev::shutdown() 已提前调用过也安全
Entity::~Entity()
{
    release(); // 保证对象无论以何种路径销毁，纹理都不会泄漏
}

// 释放纹理资源：先判空、再销毁、最后置空，保证可重复调用
void Entity::release()
{
    // 只有纹理确实存在时才销毁，避免对空指针调用 SDL_DestroyTexture
    if (m_texture != nullptr)
    {
        SDL_DestroyTexture(m_texture); // 通知 SDL 释放这张纹理占用的显存
        m_texture = nullptr;           // 置空后再次调用会直接跳过，实现幂等
    }
}

// 默认更新：无条件推进帧动画时间轴。
// 累加动作与「当前显示第几帧」无关，帧号换算发生在 render；
// 这样即使渲染帧耗时波动，动画速度也只由固定步长决定（帧率解耦）
void Entity::update(const UpdateContext& ctx)
{
    // 第一行固定动作：把上一步的位置存为插值起点。
    // 子类覆写 update 时也必须先调用它，否则 prev 会落后于 cur，
    // 渲染插值区间错误（画面滞后一步或抖动）
    snapshotPreviousPosition();

    m_animTime += ctx.deltaTime; // 累加固定步时长；不取模，保持逻辑最简
}

// 按「上一固定步位置 <-> 当前位置」的线性插值绘制
void Entity::render(SDL_Renderer* renderer, float alpha) const
{
    // 纹理尚未加载成功时直接返回，避免空指针导致崩溃
    if (m_texture == nullptr)
    {
        return; // 未加载就没有可绘制的内容
    }

    // ==== 插值位置计算（本次改造的核心渲染逻辑） ====
    // alpha=0 时取上一固定步的坐标，alpha 趋近 1 时取当前坐标；
    // 两点之间线性过渡，把逻辑步（120Hz）的离散位移铺成渲染帧上的平滑运动。
    // 注意：这只影响「画在哪」，碰撞用的 rect() 仍然返回 m_x/m_y 逻辑位置
    const float renderX = m_prevX + (m_x - m_prevX) * alpha; // 插值后的绘制 x
    const float renderY = m_prevY + (m_y - m_prevY) * alpha; // 插值后的绘制 y

    // 目标矩形：纹理画在屏幕上的位置和尺寸（碰撞盒语义与动画无关，恒为 m_w x m_h）
    const SDL_FRect dst{ renderX, renderY, m_w, m_h };

    // ==== 固定帧时长与渲染帧率解耦的帧号换算 ====
    // 用「累计逻辑时间 / 每帧时长」向下取整得到循环序号：时间攒够 m_frameTime
    // 秒就切到下一帧，与渲染帧率（60Hz/144Hz、甚至偶发卡顿）完全无关。
    // 序号再对「循环子区间的长度」取模 + 加区间起点，实现子区间内循环播放
    //（默认子区间 = 全区间，即原有行为；first == last 时长度 1、帧号恒定，
    // 为本批「跑动帧与站立帧拼一张表」提供定格能力）。
    // m_frameCount == 1 时结果恒为起点 0——单帧图与程序化回退自然退化为静止绘制

    // 防御性夹取（起点）：声明帧区间可能先于/后于真实帧数变化（如声明 5 帧的
    // 精灵表加载失败、回退到单帧图时帧数变为 1），夹到 [0, 帧数-1] 保证不越界
    const int firstFrame = std::clamp(m_animFirst, 0, m_frameCount - 1);

    // 防御性夹取（终点）：m_animLast < 0 是「全区间」约定（= 帧数 - 1，
    // 成员默认值不依赖加载后才确定的帧数）；其余情形夹到 [起点, 帧数-1]，
    // 反序声明（last < first）按定格起点处理而不是崩溃或负数长度
    const int lastFrame = (m_animLast < 0)
        ? m_frameCount - 1
        : std::clamp(m_animLast, firstFrame, m_frameCount - 1);

    // 子区间长度（含两端）：first == last 时长度为 1，帧号恒等于 first（定格）
    const int rangeLength = lastFrame - firstFrame + 1;

    // 帧号 = 区间起点 + 循环序号对区间长度取模（区间内循环播放）
    const int frameIndex =
        firstFrame + static_cast<int>(m_animTime / m_frameTime) % rangeLength;

    // srcRect 是「从精灵表里裁剪哪一块」的矩形：第 f 帧的横向起点 = f * 帧宽。
    // 精灵表只有一行帧，所以 y 恒为 0；宽高各取一个实体的大小
    const SDL_FRect src{ static_cast<float>(frameIndex) * m_w, 0.0f, m_w, m_h };

    // 翻转绘制：需要镜像时走 SDL_RenderTextureRotated——旋转 0 度、旋转中心
    // 传 nullptr（绕目标矩形自身中心）时，它与 SDL_RenderTexture 等价，只是
    // 多出翻转参数（SDL_RenderTexture 本身没有翻转能力）。
    // 未翻转时保留 SDL_RenderTexture 快路径：绝大多数实体、绝大多数时刻
    // 都不翻转，少一层旋转管线开销
    if (m_flipped)
    {
        SDL_RenderTextureRotated(renderer, m_texture, &src, &dst, 0.0, nullptr,
                                 SDL_FLIP_HORIZONTAL);
    }
    else
    {
        // SDL_RenderTexture 用 src 从整张精灵表裁出当前帧，再画到 dst 位置
        SDL_RenderTexture(renderer, m_texture, &src, &dst);
    }
}

// 返回当前位置与尺寸组成的矩形副本：AABB 碰撞判定和重生避让都依赖它。
// 刻意使用逻辑位置 m_x/m_y（而非 render 的插值位置）：碰撞在固定步内
// 用确定状态判定，渲染平滑不参与玩法
SDL_FRect Entity::rect() const
{
    // SDL_FRect 用 x,y（左上角）+ w,h（宽高）描述轴对齐矩形
    return SDL_FRect{ m_x, m_y, m_w, m_h };
}

// 把「上一固定步位置」对齐到「当前位置」。
// 调用约定见头文件：update 首行 + 瞬移之后，两处都调用同一条语句——
// 前者让本步的新位置形成 prev->cur 插值区间；后者消除插值距离防拉丝
void Entity::snapshotPreviousPosition()
{
    m_prevX = m_x; // 插值起点对齐到当前 x
    m_prevY = m_y; // 插值起点对齐到当前 y
}

// 瞬间移动：直接覆盖坐标，不经过速度/时间累积，保证重开后位置确定
void Entity::setPosition(float x, float y)
{
    m_x = x; // 覆盖 x 坐标（当前逻辑位置）
    m_y = y; // 覆盖 y 坐标（当前逻辑位置）

    // 瞬移必须同步插值快照：否则渲染会从瞬移前的旧位置「拉丝」滑过来
    //（重开归位、场景入场初始化等路径都会经过这里）
    snapshotPreviousPosition();
}

// 覆盖帧动画的每帧时长：配置驱动的动画节奏入口（如 coin_anim_interval）
void Entity::setFrameTime(float frameTime)
{
    // 帧号换算 = 累计时间 / 每帧时长：非正值（0、负数）会让换算结果无意义，
    // 属于配置错误。忽略设置并警告，保持原帧时长继续工作，避免动画坏掉
    if (frameTime <= 0.0f)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "帧时长必须为正数（收到 %f），忽略该配置", frameTime);
        return;
    }

    m_frameTime = frameTime; // 覆盖每帧时长：下一帧 render 的帧号换算立即生效
}

// 设置帧动画的循环子区间 [first, last]（含两端）
void Entity::setAnimationRange(int first, int last)
{
    // 参数校验只处理「描述不清」的输入：负起点没有帧可指（-1 是成员默认值
    // 的保留约定，不是合法入参）；last 非负却小于 first 属于反序区间
    //（想定格就写 first == last）。帧数相关越界不在此校验——加载顺序不定
    // （声明可能先于加载，回退单帧图时帧数还会变），统一交给 render 的
    // 防御性夹取，保证任何调用顺序都不越界
    if (first < 0 || (last >= 0 && last < first))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "动画帧区间非法（收到 [%d, %d]），忽略该配置", first, last);
        return;
    }

    m_animFirst = first; // 区间起点帧：循环从它开始
    m_animLast = last;   // 区间终点帧（含）；-1 = 全区间（render 解析为帧数 - 1）
}

// 设置水平翻转绘制：只写渲染标记，不触碰任何逻辑数据。
// 碰撞 rect()、插值位置与攻击判定都按未翻转的数据计算——镜像纯属
// 「画出来的样子」，不影响玩法与手感
void Entity::setFlipped(bool flipped)
{
    m_flipped = flipped; // 下一帧 render 立即生效（帧号/位置等其他逻辑不受影响）
}

// 三级回退纹理加载：把原先 Player/Coin 各自复制的加载流程收敛到这里。
// 加载顺序：精灵表 -> 单帧图 -> 程序化纯色，逐级降级并记录警告
bool Entity::loadTextureWithFallback(SDL_Renderer* renderer,
                                     const char* sheetPath,
                                     const char* singlePath,
                                     int sheetFrameCount,
                                     float frameTime,
                                     SDL_Color fallbackColor)
{
    // 帧参数先落定：单帧图与回退纹理的帧数都是 1（静止画面），
    // 只有精灵表加载成功时才把帧数改写为 sheetFrameCount，动画才会真正生效
    m_frameCount = 1;
    m_frameTime = frameTime; // 每帧时长对所有来源统一：动画节奏由子类参数决定

    // SDL_GetBasePath() 返回可执行文件所在目录的绝对路径（结尾自带路径分隔符）。
    // 用它拼素材路径，无论从哪个工作目录启动游戏，都能找到 exe 旁边的 assets/。
    // 注意版本差异：SDL2 中该函数返回需要 SDL_free 释放的 char*；SDL3 改为返回
    // const char*，字符串由 SDL 内部缓存持有，调用方既不能释放也不能修改其内容
    const char* basePath = SDL_GetBasePath();

    // 只有成功拿到基路径才尝试读文件；拿不到就跳过图片加载直接走回退
    if (basePath != nullptr)
    {
        // 第一优先：拼出精灵表完整路径 <exe目录>/<sheetPath>。
        // std::string 构造时会拷贝字符内容，后续不再依赖 SDL 的内部缓存
        std::string sheetFullPath = std::string(basePath) + sheetPath;

        // IMG_LoadTexture 是 SDL3_image 的 API：读文件 -> 解码 -> 一步创建 GPU 纹理
        m_texture = IMG_LoadTexture(renderer, sheetFullPath.c_str());

        // 精灵表加载成功：写入真实帧数，动画数据完整，首选路径达成
        if (m_texture != nullptr)
        {
            m_frameCount = sheetFrameCount; // 帧数改为精灵表帧数，动画生效
            // 像素风必需：把纹理的缩放采样设为最近邻。SDL 默认线性插值在
            // 放大时会混合相邻像素（像素画被糊成渐变），最近邻保持每个像素
            // 的硬边缘——本项目精灵均按整数倍放大显示（16x28 帧 -> 48x84
            // 等），最近邻是像素锐利的唯一选择（该调用对逻辑/内存无影响）
            SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
            return true;                    // 首选路径成功，加载结束
        }

        // 精灵表没加载成功（文件缺失/解码失败）：警告后继续尝试单帧素材
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "加载精灵表 %s 失败（%s），尝试单帧素材", sheetPath, SDL_GetError());

        // 第二优先：拼出单帧图完整路径 <exe目录>/<singlePath>
        std::string singleFullPath = std::string(basePath) + singlePath;

        // 单帧素材加载成功：帧数保持 1，动画自然退化为静止画面后返回
        m_texture = IMG_LoadTexture(renderer, singleFullPath.c_str());
        if (m_texture != nullptr)
        {
            // 同精灵表路径：单帧图同样按最近邻采样（它是精灵表的降级替代，
            // 采样方式必须一致，否则降级前后画面清晰度会出现可见差异）
            SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
            return true; // 次选路径成功，加载结束
        }
    }

    // 走到这里说明所有图片都没加载成功（含拿不到基路径的情形）：
    // 先用 SDL_LogWarn 输出具体原因，再回退到程序化纹理——素材缺失也不至于崩
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "加载精灵 %s 失败（%s），回退到程序化纹理", sheetPath, SDL_GetError());

    // ============== 以下为回退路径：程序化生成 m_w x m_h 纯色方块 ==============

    // 1. 创建一张与实体尺寸一致的内存表面（Surface 是 CPU 侧的像素数据）
    SDL_Surface* surface = SDL_CreateSurface(
        static_cast<int>(m_w),     // 表面宽度 = 实体宽（尺寸由派生类构造时固定）
        static_cast<int>(m_h),     // 表面高度 = 实体高（与精灵表单帧尺寸一致）
        SDL_PIXELFORMAT_RGBA8888); // 每像素 32 位 RGBA，通用直观

    // 表面创建失败通常意味着内存不足，把失败原样交给调用方
    if (surface == nullptr)
    {
        return false; // 纹理尚未创建，调用方负责报错
    }

    // 2. 把整张表面填充成回退颜色（MapSurfaceRGBA 会按表面像素格式生成颜色值）
    const Uint32 color = SDL_MapSurfaceRGBA(surface,
                                            fallbackColor.r,  // 红分量
                                            fallbackColor.g,  // 绿分量
                                            fallbackColor.b,  // 蓝分量
                                            fallbackColor.a); // Alpha 分量
    // dstrect 传 nullptr 表示填充整个表面区域
    SDL_FillSurfaceRect(surface, nullptr, color);

    // 3. 从表面创建 GPU 纹理；renderer 会复制一份像素数据到纹理里
    m_texture = SDL_CreateTextureFromSurface(renderer, surface);

    // 4. 像素数据已复制进纹理，表面完成使命，立刻销毁避免内存泄漏
    SDL_DestroySurface(surface);

    // 同精灵表路径：程序化回退纹理也按最近邻采样——三条加载路径的
    // 采样方式完全一致，避免「降级后观感突变」这类隐性差异
    if (m_texture != nullptr)
    {
        SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST);
    }

    // 纹理指针有效即本函数成功；回退纹理只有 1 帧（m_frameCount 保持 1），
    // 动画会自动退化为单帧静止绘制，无需在此额外标记
    return m_texture != nullptr;
}
