#define _CRT_SECURE_NO_WARNINGS

#include "ezlogger.h"

#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;

// ---------- 时间转换 ----------
std::tm localtime_safe(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

// ---------- 构造函数 ----------
Logger::Logger()
    : Logger(getExeDir()) {}

Logger::Logger(const std::string& logDir, bool openLogFile) {
    SetConsoleOutputCP(CP_UTF8);
    fs::path dir(logDir);
    std::error_code ec;
    fs::create_directories(dir, ec);   // 目录不存在则创建
    dir_ = dir.string();               // 记下目录，saveJson 用它定位输出位置

    // 用当前日期+时刻生成时间戳：日志文件名与原始数据子目录都用它，
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = localtime_safe(t);

    std::ostringstream name;
    name << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S");
    stamp_ = name.str();

    if (!openLogFile) return;          // 只要目录，不建日志文件

    fs::path filePath = dir / (stamp_ + ".log");
    file_.open(filePath, std::ios::app);

    if (!file_.is_open()) {
        std::cerr << "无法打开日志文件: " << filePath << std::endl;
    } else {
        std::cout << "日志文件: " << filePath.string() << std::endl;
    }
}

// ---------- 日志输出 ----------
void Logger::log(Level level, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = localtime_safe(t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << " [" << levelStr(level) << "] "
        << msg;

    std::string line = oss.str();
    if (enablePrint){
        std::cout << line << std::endl;
    }
    if (file_.is_open()) {
        file_ << line << std::endl;
    }
}

void Logger::test(){
    SetConsoleOutputCP(CP_UTF8);
    info("程序启动");
    debug("调试信息 x = 42");
    warn("这是一个警告");
    error("发生错误！");
}

void Logger::debug(const std::string& m) { log(DEBUG, m); }
void Logger::info (const std::string& m) { log(INFO,  m); }
void Logger::warn (const std::string& m) { log(WARN,  m); }
void Logger::error(const std::string& m) { log(ERR, m); }

// ---------- 文件输出 ----------
// 用于导出原始数据（formatRawData 生成的 JSON），便于后续复算或画图。
// 目标目录是 <本Logger目录>/<本次运行时间戳>/，第一次调用时按需创建，
// 这样多轮实验的原始数据各占一个子目录，不会互相覆盖。
bool Logger::saveJson(const std::string& fileName, const std::string& content) {
    if (saveDir_.empty()) {
        saveDir_ = (fs::path(dir_) / stamp_).string();
        std::error_code ec;
        fs::create_directories(saveDir_, ec);
    }
    const fs::path path = fs::path(saveDir_) / fileName;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        std::cerr << "无法写入文件: " << path.string() << std::endl;
        return false;
    }
    out << content;
    return out.good();
}

// ---------- 辅助 ----------
const char* Logger::levelStr(Level l) {
    switch (l) {
        case DEBUG: return "DEBUG";
        case INFO:  return "INFO";
        case WARN:  return "WARN";
        case ERR: return "ERROR";
    }
    return "?";
}

std::string Logger::getExeDir() {
#if defined(_WIN32)
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path().string();
#else
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return exe.parent_path().string();
    return fs::current_path().string();
#endif
}