#include "testFunc.h"

#include <cstdint>
#include "optCtrl.h"

// general testFunc config
namespace {
constexpr std::int32_t  kStagedIters = 2'000'000;   // 实验 1
constexpr std::uint32_t kShortIters  = 2'000'000;   // 实验 2
constexpr std::uint32_t kMergedIters = 100'000;      // 实验4
}  // namespace

// 重复计算：重复计算vs暂存结果
//  输入常量，编译期未知
volatile std::int32_t g_a1 = 1919;
volatile std::int32_t g_b1 = 810;
// 主task入口
void stagedResults(bool useStaged){
    // 读volatile，值编译期未知
    const std::int32_t a = g_a1;
    const std::int32_t b = g_b1;
    const std::int32_t c = a * b;         // 只在循环外算一次
    const std::int32_t k = a;             // 用于阻止闭式化，两变体相同
    std::uint64_t acc = 0;
    if (useStaged) {
        for (std::int32_t i = 0; i < kStagedIters; ++i) {
            acc += static_cast<std::uint64_t>(114514 + c + (i & k));
        }
    } else {
        for (std::int32_t i = 0; i < kStagedIters; ++i) {
            acc += static_cast<std::uint64_t>(114514 + (g_a1 * g_b1) + (i & k));
        }
    }
    bench::consume(acc);                  // 用于校验累加结果必须一致，保证两变体结果相同
}


// 逻辑运算：短路提前跳出vs总是两边求值
//  设计一个花销可观，且不会被编译器优化的操作
constexpr std::uint32_t kProbeSize = 1u << 16;   // 65536 项 = 256 KiB，CPU的L1缓存装不下，落在较慢的L2

//准备一个索引范围[0,65535]，共65536项的数组
std::uint32_t g_probe[kProbeSize];               // 不加static的全局变量：外部链接，编译器必须假设别的 TU 会改它
struct ProbeInit {
    ProbeInit() {
        for (std::uint32_t i = 0; i < kProbeSize; ++i)
        // 保险起见使用散列常数公式随机填充数组
            g_probe[i] = (i * 2654435761u) | 1u;  //强制把最低位变为1,一定非零
    }
} g_probeInit;//main()启动之前自动初始化一个全局对象

//哈希映射函数，把输入的连续整数i打散映射到[0, 65535]，相邻的输入得到不相邻的伪随机输出
//避免连续的输入引起顺序访问，被CPU预取器命中，强迫每次访存都去L2缓存
inline bool costlyProbe(std::uint32_t i) {
    //先乘一个大质数（Knuth 乘法散列常数），再抛弃规律性强的低13位数字保留随机性更强的高位数字
    //接着用0xFFFFF掩码映射到[0, 65535]
    const std::uint32_t idx = ((i * 2654435761u) >> 13) & (kProbeSize - 1);
    return g_probe[idx] != 0;                     //一定非零，结果恒真
}

volatile std::int32_t g_leftDecisive = 1;         // 左操作数：恒真，编译期未知防止优化掉

// 主task入口
void shortCircuit(bool useLogicalOr) {
    std::uint64_t acc = 0;
    for (std::uint32_t i = 0; i < kShortIters; ++i) {
        const bool decisive = (g_leftDecisive != 0);
        bool hit;
        if (useLogicalOr) {
            // 短路：左侧为真则右侧不求值
            hit = decisive || costlyProbe(i);     
        } else {
            // 按位或：两侧都被求值
            hit = (static_cast<int>(decisive) | static_cast<int>(costlyProbe(i))) != 0;
        }
        if (hit) acc += i;                        // 两支都产生相同累加效果，避免分支本身成为差异来源
        else     acc += 1u;                       // 若hit不同则产生不同效果，用于验证
    }
    bench::consume(acc);                          // 用于校验累加结果必须一致，保证两变体结果相同
}


// 嵌套循环：外小内大 vs 外大内小
// 用 volatile 让循环体里的 k 编译期未知，防止编译器把累加闭式化或向量化掉
volatile std::uint64_t globalNestedK = 0x9E3779B97F4A7C15ull;

// 主task入口
void nestedLoop(int outside, int inside) {
    const std::uint64_t k = globalNestedK;   // 读 volatile，值编译期未知
    std::uint64_t acc = 0;

    for (int i = 0; i < outside; ++i) {
        for (int j = 0; j < inside; ++j) {
            // 便宜的整数累加：无浮点依赖链，不会被浮点延迟掩盖循环控制开销
            acc += static_cast<std::uint64_t>(i ^ j) & k;
        }
    }

    bench::consume(acc);   // 校验和一致，保证两变体结果相同
}

//可合并的冗余循环：合并冗余vs保留冗余
//循环体内部是串行依赖链
void mergedLoop(bool useMerged) {
    float tmp = 1.0f;

    if (useMerged) {
        // 合并版：一个循环，kMergedIters 次，每次 4 个浮点运算
        for (std::uint32_t i = 0; i < kMergedIters; ++i) {
            tmp = tmp * 3.0f;
            tmp = tmp / 3.0f;
            tmp = tmp * 3.0f;
            tmp = tmp / 3.0f;
        }
    } else {
        // 不合并版：两个循环，各 kIters 次，每次 2 个浮点运算
        for (std::uint32_t i = 0; i < kMergedIters; ++i) {
            tmp = tmp * 3.0f;
            tmp = tmp / 3.0f;
        }
        for (std::uint32_t i = 0; i < kMergedIters; ++i) {
            tmp = tmp * 3.0f;
            tmp = tmp / 3.0f;
        }
    }
    bench::consume(tmp);   // 消费结果，防止整个循环被消除
}

uint64_t gcd_euclid(uint64_t m, uint64_t n) {
    for (int i = 0; i < 10000; i++){
        while (n != 0) {
            uint64_t r = m % n;
            m = n;
            n = r;
        }
        bench::consume(m);
    }
    return m;
}

uint64_t gcd_subtract(uint64_t m, uint64_t n) {
    for (int i = 0; i < 10000; i++){
        while (m != n) {
            if (m > n) m -= n;
            else       n -= m;
        }
        bench::consume(m);
    }   
    
    return m;
}