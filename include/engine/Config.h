#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <map>         // std::map：键 -> 原始字符串值的解析结果表
#include <string>      // std::string：文件路径与键值文本的存储
#include <string_view> // std::string_view：单行解析不产生拷贝

// ============================================================
// Config：数据驱动配置（engine 层）
// ============================================================
//
// 背景：窗口尺寸、玩家速度、金币数量等数值原先硬编码在 C++ 常量里，
// 想调一个数就得重新编译。本类把这些「可调数值」挪到 exe 旁边的
// config/game.ini 文本文件中——改完重启游戏即生效，无需重新编译。
//
// 设计要点：
//   1. 解析与取值分离：loadFromFile 把文件解析成「键 -> 原始字符串」
//      的表；真正的数值转换推迟到 getInt/getFloat 被调用时进行。
//      好处是解析器不关心某个键「应该是什么类型」——同一个键由使用方
//      决定按整数还是浮点读取。
//   2. 容错优先：文件缺失、坏行、缺键、非法值都不致命——记录警告后
//      回退到调用方给出的默认值，游戏永远能正常启动运行。
//   3. 默认值由调用方传入（即原先的硬编码常量），不在 Config 内部
//      维护键名与默认值清单——配置项属于使用方，增删键只改使用方一处。
//
// 用法示例：
//     Config config;
//     config.loadFromFile(std::string(SDL_GetBasePath()) + "config/game.ini");
//     const int width = config.getInt("window_width", 1280); // 缺键时回退 1280
// ============================================================

class Config
{
public:
    // 从文件加载配置。返回 true 表示文件成功读入并完成解析（坏行已被
    // 跳过并记录警告）；返回 false 表示文件缺失或无法读取——此时配置表
    // 为空，后续 getInt/getFloat 都会各自回退默认值，游戏不因此失败
    bool loadFromFile(const std::string& path);

    // 读取整数配置项：缺键或值不是合法整数时，记录警告并返回 defaultValue。
    // 本项目在启动时集中读取，正常流程每个键只会产生至多一条警告
    int getInt(const std::string& key, int defaultValue) const;

    // 读取浮点配置项（"300" 这类整数写法同样接受）：缺键或非法时同 getInt
    float getFloat(const std::string& key, float defaultValue) const;

private:
    // 解析单行文本并写入 m_values：空行 / 注释行（; 或 # 开头）直接跳过，
    // 坏行（缺少 '='、键名为空）记录警告后跳过。lineNumber 仅用于警告定位
    void parseLine(std::string_view rawLine, int lineNumber);

private:
    // 解析结果表：键 -> 原始字符串值（数值合法性在 getInt/getFloat 阶段校验）。
    // 用 std::map 而非哈希表：配置项数量少，键的遍历/查找语义更直观
    std::map<std::string, std::string> m_values;
};

#endif // !__CONFIG_H__
