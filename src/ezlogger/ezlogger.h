#pragma once

#include <fstream>
#include <string>
#include <ctime>

// 线程安全的时间转换（供内部使用，也可外部调用）
std::tm localtime_safe(std::time_t t);

class Logger {
public:
    enum Level { DEBUG, INFO, WARN, ERR };

    // 默认构造：日志目录 = exe 所在目录
    Logger();

    // 指定日志目录。
    // openLogFile = false 时只创建目录、不建日志文件，也不打印"日志文件:"那一行，
    // 适用于只需要 saveJson 落盘的场景（例如专门用来保存原始数据）。
    explicit Logger(const std::string& logDir, bool openLogFile = true);

    void log(Level level, const std::string& msg);
    void test();
    void switchPrint(bool boolean){enablePrint = boolean;}

    void debug(const std::string& m);
    void info (const std::string& m);
    void warn (const std::string& m);
    void error(const std::string& m);

    // 把一段文本（例如 JSON）原样写到 <本Logger目录>/<本次运行时间戳>/ 下的文件，
    // 同名文件会被覆盖。fileName 只给文件名，不接受路径；返回是否写入成功。
    // 每轮运行一个独立子目录，所以多轮实验的原始数据不会互相覆盖。
    bool saveJson(const std::string& fileName, const std::string& content);

    // 获取 exe 所在目录
    static std::string getExeDir();
private:
    std::ofstream file_;
    std::string   dir_;              // 本 Logger 的目录
    std::string   stamp_;            // 本次运行的时间戳（YYYY-MM-DD_HH-MM-SS）
    std::string   saveDir_;          // saveJson 的目标目录，第一次调用时按需创建
    static const char* levelStr(Level l);
    bool enablePrint = true;
};