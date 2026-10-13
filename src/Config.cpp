#include "engine/Config.h" // Config：本类声明
#include <SDL3/SDL.h>      // SDL_LoadFile / SDL_free / SDL_LogWarn / SDL_GetError
#include <charconv>        // std::from_chars：无异常的字符串 -> 数值严格解析
#include <system_error>    // std::errc：from_chars 的错误码类型

// 本编译单元私有的解析小工具（不对外暴露）
namespace
{

// 去掉字符串首尾的空白字符（空格 / 制表符 / 回车），返回值仍是原文本的视图。
// 不产生拷贝：string_view 指回原字符串的存储，适合逐行解析这种高频小操作
std::string_view trim(std::string_view text)
{
    // 左端：从头部跳过空白；整串都是空白时 find_first_not_of 返回 npos
    const size_t begin = text.find_first_not_of(" \t\r");
    if (begin == std::string_view::npos)
    {
        return std::string_view{}; // 整串都是空白：归一化为空视图，交给空行分支
    }

    // 右端：从尾部跳过空白。begin 有效时 find_last_not_of 必然有效
    const size_t end = text.find_last_not_of(" \t\r");

    // substr(起点, 长度)：长度 = 末有效字符下标 - 首有效字符下标 + 1
    return text.substr(begin, end - begin + 1);
}

} // 匿名命名空间结束

// 从文件加载配置：读整个文件 -> 逐行解析 -> 键值写入 m_values
bool Config::loadFromFile(const std::string& path)
{
    // 先清空旧数据：重复加载时不会残留上一次的键值（本项目只在启动时加载一次）
    m_values.clear();

    // 一次性读入整个文件：SDL_LoadFile 内部完成「打开 -> 读全部 -> 关闭」，
    // 返回内存块指针与字节数；比逐行 IO 简单，也不受换行缓冲差异的影响
    size_t fileSize = 0;
    void* rawData = SDL_LoadFile(path.c_str(), &fileSize);

    // 读取失败（文件不存在/无权限）：配置缺失不致命。返回 false 让使用方
    // 走默认值路径；警告在此发出，使用方不必重复判断文件是否存在
    if (rawData == nullptr)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "读取配置文件 %s 失败（%s），全部使用内置默认值",
                    path.c_str(), SDL_GetError());
        return false;
    }

    // 字节块拷贝进 std::string 后立刻用 SDL_free 归还 SDL 分配的内存：
    // 后续解析只依赖 std::string 自己管理的生命周期
    const std::string content(static_cast<const char*>(rawData), fileSize);
    SDL_free(rawData);

    // 文件起始位置：先检查并跳过 UTF-8 BOM。部分编辑器（如 Windows 记事本）
    // 保存 UTF-8 时会在文件头写入三字节 BOM \xEF\xBB\xBF；若不跳过，
    // 第一行的键名会被粘上 BOM 字节，导致该键永远查不到
    size_t pos = 0;
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
    {
        pos = 3; // 越过三字节 BOM，从真正的第一个字符开始解析
    }

    // 逐行扫描：行号从 1 开始（警告信息里用它定位问题行）
    int lineNumber = 0;
    while (pos < content.size())
    {
        ++lineNumber; // 进入新的一行

        // 本行的结束位置：找换行符；最后一行可能没有换行符
        const size_t newlinePos = content.find('\n', pos);
        const size_t lineStop = (newlinePos == std::string::npos) ? content.size() : newlinePos;

        // 交给 parseLine 解析（不含换行符的视图，零拷贝）
        parseLine(std::string_view(content.data() + pos, lineStop - pos), lineNumber);

        // 下一行起点 = 换行符之后；最后一行无换行时恰好越过末尾，循环自然结束
        pos = lineStop + 1;
    }

    // 解析完成：坏行只是被跳过，整体仍算成功
    return true;
}

// 解析单行文本：正常键值对写入 m_values；空行/注释行静默跳过；坏行警告后跳过
void Config::parseLine(std::string_view rawLine, int lineNumber)
{
    // 去掉首尾空白后的有效内容（同时处理 Windows 行尾的 '\r'）
    const std::string_view line = trim(rawLine);

    // 空行：跳过（配置文件里大量留白用于分节排版，是正常的）
    if (line.empty())
    {
        return;
    }

    // 注释行：';' 与 '#' 两种开头都当注释（ini 文件的两种常见习惯都支持）
    if (line.front() == ';' || line.front() == '#')
    {
        return;
    }

    // 定位 '=' 分隔符：找不到说明这行不是「键=值」结构，属于坏行
    const size_t equalsPos = line.find('=');
    if (equalsPos == std::string_view::npos)
    {
        // 坏行不致命：记录警告（含行号，方便定位手写错误）后跳过
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "配置文件第 %d 行缺少 '=' 分隔符，已跳过该行", lineNumber);
        return;
    }

    // 键与值分别取 '=' 两侧，再各自去空白（容忍 "key = value" 这种手写习惯）
    const std::string_view key = trim(line.substr(0, equalsPos));        // '=' 左侧：键名
    const std::string_view value = trim(line.substr(equalsPos + 1));     // '=' 右侧：值文本

    // 键名为空（形如 "=abc"）：没有键名就永远无法被查询命中，按坏行处理
    if (key.empty())
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "配置文件第 %d 行键名为空，已跳过该行", lineNumber);
        return;
    }

    // 写入解析表：同键重复出现时后写覆盖先写（「后出现者生效」，
    // 用户可以在文件末尾追加一行覆盖前面的设置，不必回头修改原行）
    m_values[std::string(key)] = std::string(value);
}

// 读取整数配置项：先查表拿原始文本，再严格解析；任何一步失败都回退默认值
int Config::getInt(const std::string& key, int defaultValue) const
{
    // 第一步：查找键
    const auto it = m_values.find(key);
    if (it == m_values.end())
    {
        // 缺键：记录警告后回退默认值。配置在启动时集中读取，
        // 正常流程每个键至多产生一条「不存在」警告
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "配置项 %s 不存在，使用默认值 %d", key.c_str(), defaultValue);
        return defaultValue;
    }

    // 第二步：严格解析整段文本为 int。from_chars 从首字符开始解析，
    // 因此 "abc"（解析失败）、"12x"（只解析出前缀）都能被识别为非法；
    // 不用 atoi / stoi 的原因：前者静默返回 0，后者以异常报告失败
    int value = 0;
    const char* begin = it->second.data();          // 文本起始指针
    const char* end = begin + it->second.size();    // 文本结束指针（开区间）
    const std::from_chars_result result = std::from_chars(begin, end, value);

    // ec 非零 = 完全没有解析出数字；ptr != end = 解析成功后还有残余字符。
    // 两种情况都算非法值：警告并回退默认值
    if (result.ec != std::errc{} || result.ptr != end)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "配置项 %s 的值 \"%s\" 不是合法整数，使用默认值 %d",
                    key.c_str(), it->second.c_str(), defaultValue);
        return defaultValue;
    }

    return value; // 解析成功：返回配置值
}

// 读取浮点配置项：流程与 getInt 完全对称，仅解析类型与警告文案不同
float Config::getFloat(const std::string& key, float defaultValue) const
{
    // 第一步：查找键
    const auto it = m_values.find(key);
    if (it == m_values.end())
    {
        // 缺键：警告并回退。%g 让 0.15 这类小数显示得干净利落
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "配置项 %s 不存在，使用默认值 %g", key.c_str(),
                    static_cast<double>(defaultValue));
        return defaultValue;
    }

    // 第二步：严格解析为 float（from_chars 对浮点同样支持「完整匹配」校验）
    float value = 0.0f;
    const char* begin = it->second.data();          // 文本起始指针
    const char* end = begin + it->second.size();    // 文本结束指针（开区间）
    const std::from_chars_result result = std::from_chars(begin, end, value);

    // 同 getInt：解析失败或存在残余字符都视为非法值。
    // 警告里的默认值显式转成 double：可变参数对 float 会做默认提升，
    // 与 %g 的期望类型精确对应，避免格式化歧义
    if (result.ec != std::errc{} || result.ptr != end)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "配置项 %s 的值 \"%s\" 不是合法数值，使用默认值 %g",
                    key.c_str(), it->second.c_str(), static_cast<double>(defaultValue));
        return defaultValue;
    }

    return value; // 解析成功：返回配置值
}
