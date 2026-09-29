#include <string>
#include "ezlogger.h"
#include "tinyOpt.h"

//======================================================================
//质量门与重测
//  一组比较实验只有同时满足下面两条，才算"干净"、才写进报告和原始数据：
//    1) 顺序偏差 [可忽略] 或 [轻微]（占比 <= 30%）
//    2) 装置自检 [通过]（A、B 各自与自己的副本比较，配对差都落在自己的 95%CI 内）
//  不满足就整组重跑，最多 kMaxTries 次；到上限仍不合格也采用最后一次
//  （否则这一组什么都没有），此时报告里会出现"重测"行，JSON 里有 attempts 字段。
//
//  质量门默认开启，可以用命令行参数关掉：
//    main.exe --no-gate    每组只测一次，合格与否都写进报告与原始数据
//  关掉之后不再有"事后筛选"，代价是顺序偏差大的数据也会混进结论。
//
//  开与不开都要如实记录：尝试次数写在报告与 JSON 里（attempts），
//  每次丢弃在日志里留一行 [重跑]，整组作废时留 [本组已跳过]。
//  按全量复核，开启时约 29% 的组需要重测（通过率 70.8%）。
//======================================================================

namespace {

//一组质量门最多尝试几次
constexpr int kMaxTries = 5;

//运行选项（只在本文件可见，由 main 解析命令行后设置）
struct Options {
    bool gate = true;   // 质量门开关：默认开
};
Options g_opt;

//用法说明
std::string usageText(const char* exe) {
    return std::string("用法: ") + exe + " [--gate | --no-gate]\n"
        "  --gate      启用质量门（默认）：顺序偏差不合格或装置自检未通过时整组重测，最多 "
        + std::to_string(kMaxTries) + " 次，只有通过的那次写进报告与原始数据\n"
        "  --no-gate   关闭质量门（-n 同义）：每组只测一次，无论合格与否都写进报告与原始数据\n";
}

//命令行解析：--gate 开启（默认），--no-gate / -n 关闭
bool parseArgs(int argc, char** argv, std::string& bad) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--gate") {
            g_opt.gate = true;
        } else if (a == "--no-gate" || a == "-n") {
            g_opt.gate = false;
        } else {
            bad = a;
            return false;
        }
    }
    return true;
}

//跑到"干净"为止；关掉质量门时只跑一次
template <class FA, class FB>
bench::Comparison runUntilClean(Logger& logger,
                                const char* nameA, FA fa,
                                const char* nameB, FB fb,
                                bench::Config cfg = bench::Config{}) {
    const int lastTry = g_opt.gate ? kMaxTries : 1;

    bench::Comparison c = bench::runCompare(nameA, fa, nameB, fb, cfg);
    for (int attempt = 2; attempt <= lastTry; ++attempt) {
        if (bench::isMeasurementClean(c)) break;   // 上次就合格了，不必再跑
        logger.info(bench::formatRetryNote(c));    // 留下"重测过"的痕迹
        c = bench::runCompare(nameA, fa, nameB, fb, cfg);
        c.attempts = attempt;
    }
    return c;
}

//质量门开着时：合格才写报告与 JSON，不合格只在日志里留一行原因；
//质量门关着时：一律写入，判定标签照常给出，筛不筛由读的人自己决定。
void reportOrSkip(Logger& logger, Logger& jsonSaver,
                  const std::string& title, const char* fileName,
                  const bench::Comparison& c) {
    if (!g_opt.gate || bench::isMeasurementClean(c)) {
        logger.info("\n" + title + "\n" + bench::formatReport(c));
        jsonSaver.saveJson(fileName, bench::formatRawData(c));
    } else {
        logger.info("\n" + title + "\n" + bench::formatSkipNote(c));
    }
}

} // namespace

int main(int argc, char** argv){
    std::string bad;
    const bool argsOk = parseArgs(argc, argv, bad);

    //报告日志：<exe目录>/log/<时间戳>.log
    Logger logger(Logger::getExeDir() + "/log/");
    //原始数据另存一份：<exe目录>/raw_data/<时间戳>/*.json
    Logger jsonSaver(Logger::getExeDir() + "/raw_data/", false);

    if (!argsOk) {
        logger.info("无法识别的参数：" + bad + "\n" + usageText(argv[0]));
        return 1;
    }

    logger.info(std::string("质量门：") + (g_opt.gate ? "开" : "关") + "  "
        + (g_opt.gate
            ? "（顺序偏差不合格或装置自检未通过就整组重测，最多 " + std::to_string(kMaxTries)
              + " 次，只有通过的那次写入报告与原始数据）"
            : "（每组只测一次，无论合格与否都写入报告与原始数据）")
        + "\n");

    //实验 1：重复计算 vs 暂存结果
    {
        const bench::Comparison c = runUntilClean(logger,
            "暂存结果", []{stagedResults(true);},
            "重复计算", []{stagedResults(false);}
        );
        reportOrSkip(logger, jsonSaver,
            "实验1：循环内重复计算 vs 循环外暂存结果",
            "exp1_staged.json", c);
    }

    //实验 2：短路提前跳出 vs 两侧都被求值
    {
        const bench::Comparison c = runUntilClean(logger,
            "提前跳出", []{shortCircuit(true);},
            "两侧求值", []{shortCircuit(false);}
        );
        reportOrSkip(logger, jsonSaver,
            "实验2：逻辑运算 短路提前跳出 vs 两侧都被求值",
            "exp2_short_circuit.json", c);
    }

    //实验 3：循环合并（合并冗余 vs 保留冗余）
    //两变体浮点运算总数都是 4e5，只差 1e5 次循环控制，预期收益很小。
    {
        const bench::Comparison c = runUntilClean(logger,
            "合并冗余", []{mergedLoop(true);},
            "保留冗余", []{mergedLoop(false);}
        );
        reportOrSkip(logger, jsonSaver,
            "实验3：冗余循环 合并冗余 vs 保留冗余",
            "exp3_loop_merge.json", c);
    }

    //实验 4：嵌套顺序
    //两变体内层循环体执行次数都是 1e5，只有条件判断次数差 2(n-m) = -1800，预期被噪声淹没。
    {
        const bench::Comparison c = runUntilClean(logger,
            "外小内大", []{nestedLoop(100, 1000);},
            "外大内小", []{nestedLoop(1000, 100);}
        );
        reportOrSkip(logger, jsonSaver,
            "实验4：嵌套循环 外小内大(100,1000) vs 外大内小(1000,100)",
            "exp4_nested_order.json", c);
    }

    //实验 5：最大公约数算法
    {
        const bench::Comparison c = runUntilClean(logger,
            "辗转相除", []{gcd_euclid(1919810114514, 1145141919810);},
            "辗转相减", []{gcd_subtract(1919810114514, 1145141919810);}
        );
        reportOrSkip(logger, jsonSaver,
            "实验 5：最大公约数算法 辗转相除 vs 辗转相减",
            "exp5_gcd_algorithm.json", c);
    }

    return 0;
}
