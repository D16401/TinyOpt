#pragma once
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <cstdint>

#include "statistics.h"

namespace bench{

//比较实验配置
struct Config {
    int    warmupRounds    = 3;      // 预热轮数（每轮 A B 各跑一遍）
    int    measureRounds   = 24;     // 正式轮数：必须是偶数，保证A先或B先的轮数都各半
    double minRoundSeconds = 0.03;   // 每轮最少耗时，用于自适应内层重复次数
    int    maxInnerReps    = 512;    // 内层重复次数上限
    bool   measureNoiseFloor = true; //主实验结束后，是否再分别测量 A、B 两个任务各自的装置噪声底。
    //噪声底 = 把该任务与自己的相同副本比较，真实差异应恒为 0。
    //其配对差的 95%CI就是装置针对该任务的分辨率下限，用于判断主实验的效应是否可信。
    //代价：实验总耗时约为原来的 3 倍（主实验 + A 自比 + B 自比）。
};

//单个函数的多轮测试统计结果
struct Series {
    std::string   name;             // 测试名
    int           rounds   = 0;     // 测量轮数。多轮测试计算置信区间，用统计方法排除噪声影响
    int           reps     = 1;     // 每轮测试中函数的重复执行次数。重复执行函数内行为提高时间开销，摊薄时钟测量开销在总时间开销中的占比。
    //每轮耗时的均值，标准差，最短轮时间，最长轮时间，中位数
    double        meanNs   = 0, sdNs = 0, minNs = 0, maxNs = 0, medianNs = 0;
    std::uint64_t checksum = 0;     //函数内行为的累加效果。可用于比较工作量是否相同
    std::vector<double> perRoundNs; // 每轮里单次调用的平均耗时
};

//装置噪声底：把一个任务与自己的相同副本比较得到的统计结果
//真实差异恒为 0，所以：
//   meanDiffNs 应约等于 0（若超出 ci95Ns，说明装置存在系统性偏差，自检不通过）
//   ci95Ns 就是装置针对该任务能分辨的最小效应（分辨率下限）
struct NoiseFloor {
    bool   measured   = false;      // 是否进行过测量（Config::measureNoiseFloor = false 时为 false）
    double meanDiffNs = 0;          // 自比配对差均值，应约等于 0
    double sdDiffNs   = 0;          // 自比配对差的标准差
    double ci95Ns     = 0;          // 自比配对差的 95%CI 半宽 = 该任务的分辨率下限
};

//效应方向判断：三条判据（区间不跨0 / 最小值差同向 / 支持轮数>=90%）全部成立才算"明确"。
//任何一条不成立，则效应方向模糊。
enum class DirectionVerdict {
    Invalid,     // 校验和不一致，比较的不是同一份工作，结论无效
    Clear,       // 效应方向明确
    Ambiguous    // 效应方向模糊
};

inline const char* directionName(DirectionVerdict d) {
    switch (d) {
        case DirectionVerdict::Invalid:   return "无效";
        case DirectionVerdict::Clear:     return "效应方向明确";
        case DirectionVerdict::Ambiguous: return "效应方向模糊";
    }
    return "未知";
}

//效应可靠性：只判断"效应能否与装置噪声区分开"（存在性），不评价幅度大小。
//判据 = 效应区间 E=[d-e, d+e] 与噪声区间 N=[-R, R] 的重合比例 f = |E∩N| / |E|
//  f <= 0.25        -> 可靠         （效应区间几乎不与噪声带重合）
//  0.25 < f <= 0.75 -> 边缘         （重合一半左右，效应与噪声同量级）
//  f > 0.75         -> 噪声淹没效应 （效应区间绝大部分落在噪声带内）
enum class Reliability {
    Invalid,       // 校验和不一致，比较的不是同一份工作，结论无效
    NoiseDrowned,  // 噪声淹没效应
    Marginal,      // 边缘
    Reliable       // 可靠
};

inline const char* reliabilityName(Reliability r) {
    switch (r) {
        case Reliability::Invalid:      return "无效";
        case Reliability::NoiseDrowned: return "噪声淹没效应";
        case Reliability::Marginal:     return "边缘";
        case Reliability::Reliable:     return "可靠";
    }
    return "未知";
}

//一组两个函数的比较结果
struct Comparison {
    Config cfg;                     // 本次测量实际使用的配置
    //主实验
    Series a, b;                    // 被测函数a, b的统计结果。本例中默认使用a为被优化变体，b为未被优化变体
    int    pairs         = 0;       // 配对数：等于Config中的measureRounds，默认24
    double meanDiffNs    = 0;       // 配对差 A - B 的均值（>0 表示 A 更慢）。先对每一轮时间求差，再求所有差的均值
    double medianDiffNs  = 0;       // 配对差的中位数（对偶发干扰更稳健）。先对每一轮时间求差，再求所有差的中位数
    double minDiffNs     = 0;       // a.minNs - b.minNs（最小值差，最保守的估计）先对每一轮时间求差，再求所有差的最小值
    double sdDiffNs      = 0;       // 配对差的标准差。先对每一轮时间求差，再求所有差的标准差
    double tStat         = 0;       // 配对差的t统计量。
    double ci95Ns        = 0;       // 配对差的95%置信区间的半宽
    int    aSlowerRounds = 0;       // a比b更慢的轮数。要么>=90%，要么<=10%，否则认为结论不明确
    int    aFasterRounds = 0;       // a比b更快的轮数（与aSlowerRounds互补，剩下的轮次为完全持平）
    double orderBiasNs   = 0;       // 残存的"后测 - 先测"，交替顺序后应约等于 0
    double orderBiasSdNs = 0;       // 顺序偏差的标准差
    double orderBiasCi95Ns = 0;     // 顺序偏差均值的95%CI半宽。偏差落在它之内 => 与0不可区分
    bool   checksumMatch = true;    // a和b的checksum是否相同
    int    attempts      = 1;       // 这组数据是第几次尝试得到的（>1 表示前几次因顺序偏差/装置自检不合格被丢弃）
    double ratio         = 1.0;     // b.meanNs / a.meanNs
    double minRatio      = 1.0;     // b.minNs  / a.minNs
    std::vector<double> diffPerRoundNs;       // 逐轮配对差 A - B（ns）
    std::vector<double> orderDiagPerRoundNs;  // 逐轮"后测 - 先测"（ns）

    //效应方向和可靠性判断
    NoiseFloor  noiseA, noiseB;             // A、B 各自的噪声分辨率
    bool        noiseSelfCheckOk = true;    // 两个自比结果是否都落在自己的 95%CI 内（装置自检）
    double      resolutionNs     = 0;       // 装置分辨率下限 = max(noiseA.ci95Ns, noiseB.ci95Ns)
    double      snr              = 0;       // 信噪比 = |meanDiffNs| / resolutionNs
    double      noiseOverlap     = 0;       // 效应区间与噪声区间的重合比例 f = |E∩N| / |E|，可靠性由它决定
    DirectionVerdict direction   = DirectionVerdict::Ambiguous;  // 效应方向判断
    Reliability reliability      = Reliability::NoiseDrowned;    // 综合可靠性分级（默认取最弱）
};

//顺序偏差的判定等级
enum class OrderBiasLevel {
    Unknown,      // 基准为 0，无法判断
    Negligible,   // [可忽略]
    Slight,       // [轻微]
    Significant,  // [显著偏差]：真实存在且足以干扰结论
    Volatile      // [大幅波动]：幅度不小，但统计上还没证实它存在
};
inline const char* orderBiasName(OrderBiasLevel l) {
    switch (l) {
        case OrderBiasLevel::Negligible:  return "[可忽略]";
        case OrderBiasLevel::Slight:      return "[轻微]";
        case OrderBiasLevel::Significant: return "[显著偏差]";
        case OrderBiasLevel::Volatile:    return "[大幅波动]";
        case OrderBiasLevel::Unknown:     break;
    }
    return "[未知]";
};
OrderBiasLevel orderBiasLevel(const Comparison& c);
//报告里用的顺序偏差标签
inline std::string orderBiasTag(const Comparison& c) {
    return orderBiasName(orderBiasLevel(c));
}
//顺序偏差是否可以接受（重测门限就设在"可忽略或轻微"）
inline bool orderBiasAcceptable(const Comparison& c) {
    const OrderBiasLevel l = orderBiasLevel(c);
    return l == OrderBiasLevel::Negligible || l == OrderBiasLevel::Slight;
}
//这一组测量是否"干净"：顺序偏差可接受 且 装置自检通过。
//main 用它决定是"采用并写入"还是"丢弃并重跑"。
inline bool isMeasurementClean(const Comparison& c) {
    return orderBiasAcceptable(c) && c.noiseSelfCheckOk;
}

// 格式化输出实验结果
// 字符串格式化。传入时间数字的单位为纳秒，输出相应单位的字符串（ms,us,ns）
inline std::string fmtNs(double ns) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    const double a = std::fabs(ns);
    if (a >= 1e6)      o << ns / 1e6 << " ms";
    else if (a >= 1e3) o << ns / 1e3 << " us";
    else               o << ns       << " ns";
    return o.str();
}
//格式化单个task实验的结果
inline std::string formatSeries(const Series& s) {
    std::ostringstream o;
    o << "单次 " << fmtNs(s.meanNs) << " ± " << fmtNs(s.sdNs)
      << " | min " << fmtNs(s.minNs)
      << " | median " << fmtNs(s.medianNs)
      << " | max " << fmtNs(s.maxNs);
    return o.str();
}

//方向标签：按配对差均值判断谁更快。
//meanDiffNs = A - B，>0 表示 A 的耗时更大（A 更慢），也就是 B 更快。
inline std::string directionTag(const Comparison& c) {
    if (c.meanDiffNs > 0.0) return "[B比A快]";
    if (c.meanDiffNs < 0.0) return "[A比B快]";
    return "[两者相当]";
}
//最终结论：把"方向判断"和"可靠性等级"两个正交判据合成一句话
//  约定：A 是被优化变体、B 是基线，所以是"优化"还是"劣化"看报告上面的方向行
//
//                  | 可靠                      | 边缘                      | 噪声淹没效应
//  效应方向明确     | 对性能有显著的优化/劣化作用 | 对性能有轻微的优化/劣化作用 | 对性能有可忽略的优化/劣化作用
//  效应方向模糊     | 会引起性能的显著波动       | 会引起性能的轻微波动       | 无法确认对性能有显著影响
//  无效            | 实验控制变量设计存在致命问题（两个变体工作量不相同）
inline std::string conclusionText(const Comparison& c) {
    //工作量不一致：讨论优化、波动都没有意义，直接判设计问题
    if (!c.checksumMatch || c.reliability == Reliability::Invalid ||
        c.direction == DirectionVerdict::Invalid) {
        return "实验控制变量设计存在致命问题：两个变体做的不是同一份逻辑工作";
    }

    const bool dirClear = (c.direction == DirectionVerdict::Clear);
    switch (c.reliability) {
        case Reliability::Reliable:
            return dirClear ? "对性能有显著的优化/劣化作用"
                            : "会引起性能的显著波动";
        case Reliability::Marginal:
            return dirClear ? "对性能有轻微的优化/劣化作用"
                            : "会引起性能的轻微波动";
        case Reliability::NoiseDrowned:
            return dirClear ? "对性能有可忽略的优化/劣化作用"
                            : "无法确认对性能有显著影响";
        case Reliability::Invalid:
            break;   // 上面已经处理过
    }
    return "实验控制变量设计存在致命问题：两个变体做的不是同一份逻辑工作";
}

//重测提示：这一组第 attempts 次尝试不合格，整组重跑。
//只在日志里留一行，被丢弃的那次数据不写进报告，也不存 JSON —— 但留下痕迹，
//这样"重测筛选"这件事是可审计的，不是隐形的。
inline std::string formatRetryNote(const Comparison& c) {
    std::ostringstream o;
    o << "  [重跑] 第 " << c.attempts << " 次尝试未通过质量门：顺序偏差 "
      << orderBiasTag(c) << "（" << fmtNs(c.orderBiasNs) << "）"
      << "，装置自检 " << (c.noiseSelfCheckOk ? "[通过]" : "[未通过]")
      << " —— 丢弃本次数据，重新测量\n";
    return o.str();
}

//装置自检：自比的真值恒为 0，所以自比配对差若落到它自己的 95%CI 之外，
//说明该任务存在系统性偏差（不是随机噪声），此时测出的噪声底与分辨率不可信。
//正常情况下仍有约 5% 概率随机越界，所以判为[显著偏差]时建议复测确认，而不是直接判无效。
inline std::string noiseSelfCheckTag(const Comparison& c) {
    if (c.noiseSelfCheckOk) return "[通过]";

    std::ostringstream o;
    o << "[显著偏差]  ";
    bool first = true;
    //逐条列出到底哪个变体越界、越了多少
    const auto report = [&](const char* who, const NoiseFloor& nf) {
        if (std::fabs(nf.meanDiffNs) <= nf.ci95Ns) return;   // 这一侧没越界
        if (!first) o << "；";
        first = false;
        o << who << " 自比配对差 " << fmtNs(nf.meanDiffNs)
          << " 超出自身 95%CI (±" << fmtNs(nf.ci95Ns) << ")";
    };
    report("A", c.noiseA);
    report("B", c.noiseB);
    o << " —— 自比真值恒为 0，越界说明存在系统性偏差而非随机噪声，"
      << "该任务的噪声底与分辨率估计不可信，建议复测";
    return o.str();
}

//格式化单个噪声底（自比）结果
inline std::string formatNoiseFloor(const NoiseFloor& nf) {
    std::ostringstream o;
    if (!nf.measured) {
        o << "未测量";
        return o.str();
    }
    o << "自比配对差 " << fmtNs(nf.meanDiffNs)
      << " | sd " << fmtNs(nf.sdDiffNs)
      << " | 95%CI ± " << fmtNs(nf.ci95Ns);
    return o.str();
}

//不合格组的说明：这一组重测到上限仍没通过质量门（顺序偏差可忽略或轻微 且 装置自检通过），
//所以不写入报告与原始数据，只在日志里留一行原因，避免把带顺序效应的数据混进最终结论。
inline std::string formatSkipNote(const Comparison& c) {
    const double base = std::max(std::fabs(c.meanDiffNs), c.resolutionNs);

    std::ostringstream o;
    o << "[本组已跳过] 连续 " << c.attempts
      << " 次尝试都没通过质量门，本组不写入报告与原始数据\n";

    o << "  顺序偏差        " << orderBiasTag(c)
      << "  后测-先测 = " << fmtNs(c.orderBiasNs)
      << " | 95%CI ± " << fmtNs(c.orderBiasCi95Ns);
    if (base > 0.0) {
        o << " | 占基准 " << std::fixed << std::setprecision(2)
          << (std::fabs(c.orderBiasNs) / base) << " 倍";
    }
    o << "\n";

    o << "  装置自检        " << noiseSelfCheckTag(c) << "\n";

    o << "  效应(A-B)       " << fmtNs(c.meanDiffNs)
      << " | 噪声分辨率 " << fmtNs(c.resolutionNs)
      << " | 判定 [方向 " << directionName(c.direction)
      << " / 可靠性 " << reliabilityName(c.reliability) << "]\n";

    o << "  说明            顺序偏差会按固定的先后次序系统性地偏向某一侧，";
    if (!c.noiseSelfCheckOk) o << "装置自检越界说明噪声底本身不可信，";
    o << "两者都会让效应量偏离真实值，该组数据整体作废，报告中不含这一组。\n";
    return o.str();
}

//为部分情况补充的说明
//根据reliability,进行补充说明
//当判定为"无效""边缘"时，补充解释说明；
//缺噪声底数据时，补充解释说明，
inline std::string reliabilityNote(const Comparison& c) {
    if (!c.noiseA.measured || !c.noiseB.measured) {
        return "  未测噪声底，无噪声-效应区间重合判断，仅按效应区间是否跨 0 进行判定";
    }
    switch (c.reliability) {
        case Reliability::Invalid:
            return "  校验和不一致，两个变体做的不是同一份逻辑工作，本组比较实验无效";
        case Reliability::Marginal: {
            std::ostringstream o;
            o << "  效应区间与噪声区间重合 " << std::fixed << std::setprecision(0)
              << (100.0 * c.noiseOverlap) << "%，效应与装置噪声同量级";
            return o.str();
        }
        case Reliability::Reliable:
        case Reliability::NoiseDrowned: {
            std::ostringstream o;
            o << "  效应区间与噪声区间重合 " << std::fixed << std::setprecision(0)
              << (100.0 * c.noiseOverlap) << "%";
            return o.str();
        }
        default:
            return std::string();
    }
}

//格式化比较实验的结果报告
inline std::string formatReport(const Comparison& c) {
    std::ostringstream o;

    o << "  A " << c.a.name << "      reps=" << c.a.reps << "  " << formatSeries(c.a)
      << "  (n=" << c.pairs << ")\n";
    o << "  B " << c.b.name << "      reps=" << c.b.reps << "  " << formatSeries(c.b)
      << "  (n=" << c.pairs << ")\n";

    o << "  配对差(A-B)     均值 " << fmtNs(c.meanDiffNs)
      << " | sd " << fmtNs(c.sdDiffNs)
      << " | t = " << std::fixed << std::setprecision(1) << c.tStat
      << " | 95%CI ± " << fmtNs(c.ci95Ns)
      << " | 中位数 " << fmtNs(c.medianDiffNs)
      << " | 最小值差 " << fmtNs(c.minDiffNs) << "\n";

    o << "  方向            " << directionTag(c)
      << "  A更快 " << c.aFasterRounds << "/" << c.pairs
      << " 轮 | B更快 " << c.aSlowerRounds << "/" << c.pairs << " 轮\n";

    o << "  效应比值        B/A = " << std::setprecision(3) << c.ratio
      << " | 按最小值 = " << c.minRatio << "\n";

    o << "  顺序偏差        " << orderBiasTag(c)
      << "  后测-先测 = " << fmtNs(c.orderBiasNs) << "\n";

    o << "  校验和          " << (c.checksumMatch ? "[一致]" : "[不一致]")
      << "  A=" << c.a.checksum << "  B=" << c.b.checksum << "\n";

    if (c.attempts > 1) {
        o << "  重测            第 " << c.attempts << " 次尝试通过质量门"
          << "（前 " << (c.attempts - 1) << " 次因顺序偏差过大或装置自检未通过被丢弃）\n";
    }

    if (c.noiseA.measured || c.noiseB.measured) {
        o << "  噪声底A         " << formatNoiseFloor(c.noiseA) << "\n";
        o << "  噪声底B         " << formatNoiseFloor(c.noiseB) << "\n";
        o << "  装置自检        " << noiseSelfCheckTag(c) << "\n";
        o << "  噪声区间        [" << fmtNs(-c.resolutionNs) << ", "
          << fmtNs(c.resolutionNs) << "]"
          << "   噪声分辨率 " << fmtNs(c.resolutionNs) << "\n";
        o << "  效应区间        [" << fmtNs(c.meanDiffNs - c.ci95Ns) << ", "
          << fmtNs(c.meanDiffNs + c.ci95Ns) << "]"
          << " | 重合比例 " << std::fixed << std::setprecision(2) << c.noiseOverlap << "\n";
    }

    o << "  方向判断        [" << directionName(c.direction) << "]\n";
    o << "  可靠性等级      [" << reliabilityName(c.reliability) << "]"
      << reliabilityNote(c) << "\n";
    o << "  结论            " << conclusionText(c) << "\n";
    return o.str();
}

inline std::string jsonStr(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (char c : s) {
        const unsigned char ch = static_cast<unsigned char>(c);
        switch (ch) {
            case '"':  o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\n': o << "\\n";  break;
            case '\r': o << "\\r";  break;
            case '\t': o << "\\t";  break;
            default:
                if (ch < 0x20) {   // 其余不可见控制字符用 \u00XX
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                      << static_cast<int>(ch) << std::dec << std::setfill(' ');
                } else {
                    o << c;        // UTF-8 字节原样输出
                }
        }
    }
    o << '"';
    return o.str();
}

//浮点 -> JSON 数字。非有限值（nan / inf）写成 null，保证输出始终是合法 JSON
inline std::string jsonNum(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream o;
    o << std::setprecision(10) << v;   // 10 位有效数字，够保留 ns 级细节又不会太长
    return o.str();
}
inline std::string jsonBool(bool b) { return b ? "true" : "false"; }
//一维浮点数组 -> JSON 数组（整体放一行，避免逐轮数据把日志撑成几百行）
inline std::string jsonNums(const std::vector<double>& v) {
    std::ostringstream o;
    o << '[';
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) o << ", ";
        o << jsonNum(v[i]);
    }
    o << ']';
    return o.str();
    
}
// 原始数据导出：把一次比较实验的全部原始数据与统计量按 JSON 语法输出成一个字符串
// 约定：
//   - 所有时间量单位都是纳秒(ns)，字段名以 Ns 结尾；
//   - perRoundNs / diffPerRoundNs / orderDiagPerRoundNs 是逐轮原始测量值，不是统计量；
//   - checksum 是 64 位整数。JSON 规范不限制整数精度，但 JavaScript 解析器在 2^53 以上会丢精度；
//   - 非有限值一律写成 null，字符串按 JSON 规则转义，所以输出始终是合法 JSON；
//   - verdict 里的两项是判定结果（文字），其余字段都是数据。
inline std::string formatRawData(const Comparison& c) {
    //单个变体的对象
    const auto seriesJson = [](const Series& s) {
        std::ostringstream o;
        o << "{\n"
          << "    \"name\": " << jsonStr(s.name) << ",\n"
          << "    \"reps\": " << s.reps << ",\n"
          << "    \"rounds\": " << s.rounds << ",\n"
          << "    \"meanNs\": " << jsonNum(s.meanNs) << ",\n"
          << "    \"sdNs\": " << jsonNum(s.sdNs) << ",\n"
          << "    \"minNs\": " << jsonNum(s.minNs) << ",\n"
          << "    \"maxNs\": " << jsonNum(s.maxNs) << ",\n"
          << "    \"medianNs\": " << jsonNum(s.medianNs) << ",\n"
          << "    \"checksum\": " << s.checksum << ",\n"
          << "    \"perRoundNs\": " << jsonNums(s.perRoundNs) << "\n"
          << "  }";
        return o.str();
    };
    //单个噪声底对象
    const auto noiseJson = [](const NoiseFloor& nf) {
        std::ostringstream o;
        o << "{ \"measured\": " << jsonBool(nf.measured)
          << ", \"meanDiffNs\": " << jsonNum(nf.meanDiffNs)
          << ", \"sdDiffNs\": " << jsonNum(nf.sdDiffNs)
          << ", \"ci95Ns\": " << jsonNum(nf.ci95Ns) << " }";
        return o.str();
    };

    std::ostringstream o;

    o << "{\n";

    o << "  \"config\": {\n"
      << "    \"warmupRounds\": " << c.cfg.warmupRounds << ",\n"
      << "    \"measureRounds\": " << c.cfg.measureRounds << ",\n"
      << "    \"minRoundSeconds\": " << jsonNum(c.cfg.minRoundSeconds) << ",\n"
      << "    \"maxInnerReps\": " << c.cfg.maxInnerReps << ",\n"
      << "    \"measureNoiseFloor\": " << jsonBool(c.cfg.measureNoiseFloor) << "\n"
      << "  },\n";

    o << "  \"A\": " << seriesJson(c.a) << ",\n";
    o << "  \"B\": " << seriesJson(c.b) << ",\n";

    o << "  \"compare\": {\n"
      << "    \"pairs\": " << c.pairs << ",\n"
      << "    \"meanDiffNs\": " << jsonNum(c.meanDiffNs) << ",\n"
      << "    \"medianDiffNs\": " << jsonNum(c.medianDiffNs) << ",\n"
      << "    \"minDiffNs\": " << jsonNum(c.minDiffNs) << ",\n"
      << "    \"sdDiffNs\": " << jsonNum(c.sdDiffNs) << ",\n"
      << "    \"tStat\": " << jsonNum(c.tStat) << ",\n"
      << "    \"ci95Ns\": " << jsonNum(c.ci95Ns) << ",\n"
      << "    \"aFasterRounds\": " << c.aFasterRounds << ",\n"
      << "    \"aSlowerRounds\": " << c.aSlowerRounds << ",\n"
      << "    \"orderBiasNs\": " << jsonNum(c.orderBiasNs) << ",\n"
      << "    \"orderBiasSdNs\": " << jsonNum(c.orderBiasSdNs) << ",\n"
      << "    \"orderBiasCi95Ns\": " << jsonNum(c.orderBiasCi95Ns) << ",\n"
      << "    \"checksumMatch\": " << jsonBool(c.checksumMatch) << ",\n"
      << "    \"attempts\": " << c.attempts << ",\n"
      << "    \"ratio\": " << jsonNum(c.ratio) << ",\n"
      << "    \"minRatio\": " << jsonNum(c.minRatio) << ",\n"
      << "    \"diffPerRoundNs\": " << jsonNums(c.diffPerRoundNs) << ",\n"
      << "    \"orderDiagPerRoundNs\": " << jsonNums(c.orderDiagPerRoundNs) << "\n"
      << "  },\n";

    o << "  \"noiseFloor\": {\n"
      << "    \"A\": " << noiseJson(c.noiseA) << ",\n"
      << "    \"B\": " << noiseJson(c.noiseB) << ",\n"
      << "    \"noiseSelfCheckOk\": " << jsonBool(c.noiseSelfCheckOk) << ",\n"
      << "    \"resolutionNs\": " << jsonNum(c.resolutionNs) << ",\n"
      << "    \"overlapFraction\": " << jsonNum(c.noiseOverlap) << ",\n"
      << "    \"snr\": " << jsonNum(c.snr) << "\n"
      << "  },\n";

    o << "  \"verdict\": {\n"
      << "    \"direction\": " << jsonStr(directionName(c.direction)) << ",\n"
      << "    \"reliability\": " << jsonStr(reliabilityName(c.reliability)) << "\n"
      << "  }\n";

    o << "}\n";
    return o.str();
}

} //namespace bench