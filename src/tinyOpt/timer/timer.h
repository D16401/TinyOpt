#pragma once

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>
#include <sstream>
#include <iomanip>

#include "optCtrl.h"
#include "statistics.h"
#include "formater.h"

namespace bench{

//高精度单调递增时钟（MSVC上由 QPC 支撑实现）
using Clock = std::chrono::steady_clock;

/*
结论判断：
    1.判断工作量：
        checksumMatch = false -> 不相同 -> [无效]
        checksumMatch = true -> 相同 -> 判断2和3
    2.判断效应方向：
        - 区间估计不跨 0：|meanDiffNs| > ci95Ns
        - 最小值差与均值差同方向
        - 支持结论方向的轮数 >= 90%
        全部成立      -> [效应方向明确]
        任何一条不成立 -> [效应方向模糊]
    3.判断效应可靠性
        计算噪声的95%CI和效应的95%CI的重合区间，占效应的95%CI的百分比
        效应区间 E = [d-e, d+e]，其中 d = |meanDiffNs|，e = ci95Ns
        噪声区间 N = [-R, R]，其中 R = 噪声分辨率 = max(两个噪声底的 95%CI 半宽)，理论中心取 0
        重合比例 f = |E ∩ N| / |E|
        f <= 0.25         -> [可靠]
        0.25 < f <= 0.75  -> [边缘]（余量不足，结论不稳定）
        f > 0.75          -> [噪声淹没效应]
*/

// 连续调用 task 共 reps 次，返回单次调用的平均耗时（ns）。
// 用批次计时，计时区间内重复执行ask，把约100 ns的时钟读取开销摊薄。
template <class Task>
double timeBatch(Task& task, int reps) {
    const auto t0 = Clock::now();
    for (int r = 0; r < reps; ++r) task();
    const auto t1 = Clock::now();
    clobberMemory();
    return std::chrono::duration<double, std::nano>(t1 - t0).count()
         / static_cast<double>(reps);
}

// 根据task单次执行耗时和Config的最小单轮用时minRoundSeconds，计算出每轮的自适应重复执行次数
// 保证一次批次耗时 >= cfg.minRoundSeconds，摊薄时钟开销
// 尽管只跑一次task的计时数据并不稳定，算出来的reps比较粗略
// 但这里只保证多次task累计出的时间有足够的量级，稳定性靠正式测量的其他统计手段
template <class Task>
int calibrateReps(Task& task, const Config& cfg) {
    const auto t0 = Clock::now();
    task();
    const auto t1 = Clock::now();
    const double one = std::chrono::duration<double>(t1 - t0).count();
    if (!(one > 0.0)) return cfg.maxInnerReps;          // 快到测不出来：直接取最大值
    double reps = std::ceil(cfg.minRoundSeconds / one);
    if (reps < 1.0) reps = 1.0;
    if (reps > static_cast<double>(cfg.maxInnerReps)) reps = cfg.maxInnerReps;
    return static_cast<int>(reps);
}

// 对比任务核心函数
template <class FA, class FB>
Comparison runCompareCore(const std::string& nameA, FA&& fa,
               const std::string& nameB, FB&& fb,
               const Config& cfg) {

    Comparison cmpr;
    cmpr.cfg = cfg;
    cmpr.a.name = nameA;
    cmpr.b.name = nameB;

    //标定两个task各自的重复次数
    const int repsA = calibrateReps(fa, cfg);
    const int repsB = calibrateReps(fb, cfg);
    cmpr.a.reps = repsA;
    cmpr.b.reps = repsB;

    //warm up 预热 指令缓存、数据页、分支预测器、CPU 频率，性能爬坡
    for (int r = 0; r < cfg.warmupRounds; ++r) {
        for (int k = 0; k < repsA; ++k) fa();
        for (int k = 0; k < repsB; ++k) fb();
    }
    clobberMemory();//内存屏障

    //正式测量任务
    const int rounds = std::max(2, cfg.measureRounds);///起码2轮
    std::vector<double> time_a, time_b, diff, orderDiag;
    time_a.reserve(rounds);
    time_b.reserve(rounds);
    diff.reserve(rounds);
    orderDiag.reserve(rounds);

    for (int r = 0; r < rounds; ++r){
        const bool a_First = (r%2 == 0);
        double x = 0.0, y = 0.0;
        if (a_First){//a先
            x = timeBatch(fa, repsA); y = timeBatch(fb, repsB);
        }else{//b先
            y = timeBatch(fb, repsB); x = timeBatch(fa, repsA);
        }
        time_a.push_back(x);
        time_b.push_back(y);
        diff.push_back(x - y);
        orderDiag.push_back(a_First ? (y - x) : (x - y));   // 统一计算式 "后测 - 先测"
    }

    //校验累加效果：确认两个变体做的是同一份工作
    //fa和fb内部必须使用bench::consume修改bench::globalSink
    fa();
    const std::uint64_t sumA = bench::globalSink;
    fb();
    const std::uint64_t sumB = bench::globalSink;
    cmpr.a.checksum = sumA;
    cmpr.b.checksum = sumB;
    cmpr.checksumMatch = (sumA == sumB);

    //统计方法分析
    // 基本统计量
    cmpr.a.rounds = rounds;
    cmpr.b.rounds = rounds;
    
    cmpr.a.meanNs   = detail::meanOf(time_a);
    cmpr.a.sdNs     = detail::sdOf(time_a);
    cmpr.a.minNs    = *std::min_element(time_a.begin(), time_a.end());
    cmpr.a.maxNs    = *std::max_element(time_a.begin(), time_a.end());
    cmpr.a.medianNs = detail::medianOf(time_a);

    cmpr.b.meanNs   = detail::meanOf(time_b);
    cmpr.b.sdNs     = detail::sdOf(time_b);
    cmpr.b.minNs    = *std::min_element(time_b.begin(), time_b.end());
    cmpr.b.maxNs    = *std::max_element(time_b.begin(), time_b.end());
    cmpr.b.medianNs = detail::medianOf(time_b);

    cmpr.pairs         = rounds;
    cmpr.meanDiffNs    = detail::meanOf(diff);
    cmpr.medianDiffNs  = detail::medianOf(diff);
    cmpr.minDiffNs     = cmpr.a.minNs - cmpr.b.minNs;
    cmpr.sdDiffNs      = detail::sdOf(diff);

    // 统计检验：t分布 置信区间
    const double sem   = cmpr.sdDiffNs / std::sqrt(static_cast<double>(rounds));
    cmpr.ci95Ns        = detail::tCrit95(rounds - 1) * sem;
    cmpr.tStat         = (sem > 0.0) ? (cmpr.meanDiffNs / sem) : 0.0;

    // 计数数据的方向
    cmpr.aSlowerRounds = static_cast<int>(
        std::count_if(diff.begin(), diff.end(), [](double d) { return d > 0.0; }));
    cmpr.aFasterRounds = static_cast<int>(
        std::count_if(diff.begin(), diff.end(), [](double d) { return d < 0.0; }));

    //测量顺序的误差：均值 + 它自己的置信区间（用来判断这个偏差是不是真实存在）
    cmpr.orderBiasNs      = detail::meanOf(orderDiag);
    cmpr.orderBiasSdNs    = detail::sdOf(orderDiag);
    cmpr.orderBiasCi95Ns  = detail::tCrit95(rounds - 1) * cmpr.orderBiasSdNs
                          / std::sqrt(static_cast<double>(rounds));

    //结论比值
    cmpr.ratio            = (std::fabs(cmpr.a.meanNs) > 0.0) ? (cmpr.b.meanNs / cmpr.a.meanNs) : 1.0;
    cmpr.minRatio         = (std::fabs(cmpr.a.minNs)  > 0.0) ? (cmpr.b.minNs  / cmpr.a.minNs)  : 1.0;

    //保留逐轮原始数据
    cmpr.a.perRoundNs.assign(time_a.begin(), time_a.end());
    cmpr.b.perRoundNs.assign(time_b.begin(), time_b.end());
    cmpr.diffPerRoundNs.assign(diff.begin(), diff.end());
    cmpr.orderDiagPerRoundNs.assign(orderDiag.begin(), orderDiag.end());
    return cmpr;
}
//判断效应方向:
// 三条判据全部成立 -> [效应方向明确]
// 任何一条不成立   -> [效应方向模糊]
inline DirectionVerdict classifyDirection(const Comparison& c) {
    if (!c.checksumMatch) return DirectionVerdict::Invalid;

    const bool isCiOk  = std::fabs(c.meanDiffNs) > c.ci95Ns;
    const bool isDirOk = std::max(c.aFasterRounds, c.aSlowerRounds) >= (c.pairs * 9) / 10;
    const bool isRobustOk = (c.minDiffNs == 0.0) ||
                            ((c.minDiffNs > 0.0) == (c.meanDiffNs > 0.0));

    if (!isCiOk || !isDirOk || !isRobustOk) return DirectionVerdict::Ambiguous;
    return DirectionVerdict::Clear;
}

//判断效应可靠性：效应能否与装置噪声区分开
//  计算噪声的95%CI和效应的95%CI的重合区间，占效应的95%CI的百分比
inline Reliability classifyReliability(const Comparison& c) {
    if (!c.checksumMatch) return Reliability::Invalid;

    // 没有测噪声底时做不了区间重合判断
    if (!c.noiseA.measured || !c.noiseB.measured) return Reliability::Reliable;

    if (c.noiseOverlap > 0.75) return Reliability::NoiseDrowned;
    if (c.noiseOverlap > 0.25) return Reliability::Marginal;
    return Reliability::Reliable;
}

//全量实验入口：主对照实验 + A/B 各自的噪声底 + 可靠性分级
// 步骤：
//   1. 主实验 runCompareCore(A, B)  —— 得到效应与它的 95%CI
//   2. A自比  runCompareCore(A, A)  —— 真实差异恒为 0，其 95%CI 就是 A 的分辨率下限
//   3. B自比  runCompareCore(B, B)  —— 同上，针对 B
//   4. 取两者较大者为噪声分辨率，计算与效应95%CI的重合区间占比
//   5. 根据以上实验和计算结果，给出效应的方向和可靠性分级
template <class FA, class FB>
Comparison runCompare(const std::string& nameA, FA&& fa,
                      const std::string& nameB, FB&& fb,
                      const Config& cfg = Config{}) {

    // 主实验
    Comparison cmpr = runCompareCore(nameA, fa, nameB, fb, cfg);

    // 测量两个变体各自的噪声底
    if (cfg.measureNoiseFloor) {
        const Comparison selfA = runCompareCore(nameA + "(自比)", fa, nameA + "(自比副本)", fa, cfg);
        const Comparison selfB = runCompareCore(nameB + "(自比)", fb, nameB + "(自比副本)", fb, cfg);

        cmpr.noiseA = NoiseFloor{ true, selfA.meanDiffNs, selfA.sdDiffNs, selfA.ci95Ns };
        cmpr.noiseB = NoiseFloor{ true, selfB.meanDiffNs, selfB.sdDiffNs, selfB.ci95Ns };

        // 自检：自比的真值是 0，若配对差均值落到它自己的 95%CI 之外，
        // 则说明存在系统性偏差（95%的把握，仍有5%小概率，故只做提示）
        const bool okA = std::fabs(cmpr.noiseA.meanDiffNs) <= cmpr.noiseA.ci95Ns;
        const bool okB = std::fabs(cmpr.noiseB.meanDiffNs) <= cmpr.noiseB.ci95Ns;
        cmpr.noiseSelfCheckOk = okA && okB;

        // 分辨率下限：取两者中较大的噪声底（保守估计）
        cmpr.resolutionNs = std::max(cmpr.noiseA.ci95Ns, cmpr.noiseB.ci95Ns);
    }

    // 误差区间与重合比例
    //    效应区间 E = [d-e, d+e]（d 取绝对值；噪声区间对称，所以只看大小）
    //    噪声区间 N = [-R, R]（自比真值恒为 0，故取理论中心 0）
    //    重合比例 f = |E ∩ N| / |E|
    const double d = std::fabs(cmpr.meanDiffNs);
    const double e = cmpr.ci95Ns;
    const double R = cmpr.resolutionNs;
    if (e > 0.0 && R > 0.0) {
        const double lo = std::max(d - e, -R);
        const double hi = std::min(d + e,  R);
        cmpr.noiseOverlap = std::max(0.0, hi - lo) / (2.0 * e);
        if (cmpr.noiseOverlap > 1.0) cmpr.noiseOverlap = 1.0;
    }
    cmpr.snr = (R > 0.0) ? (d / R) : 0.0;   // 信噪比仅供参考，不参与判级

    // 给出效应的方向
    cmpr.direction   = classifyDirection(cmpr);
    // 给出效应的可靠性分级
    cmpr.reliability = classifyReliability(cmpr);
    return cmpr;
}

}   //namespace bench