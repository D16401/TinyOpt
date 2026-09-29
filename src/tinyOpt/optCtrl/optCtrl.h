#pragma once

#include <cstdint>
#include <cstring>          // std::memcpy：按字节搬运位模式
#include <type_traits>      // std::is_integral_v / std::is_floating_point_v


#if defined(_MSC_VER)
    #include <intrin.h>          // _ReadWriteBarrier
#endif

namespace bench{

//写入volatile变量，防止编译器优化掉
//编译器不会修改对volatile变量的读写操作
inline volatile std::uint64_t globalSink = 0;
//取一个浮点数的"位模式"，按字节原样搬进无符号整数。
//对任何浮点值都合法：inf、NaN、超出整数范围的值都不会触发未定义行为。
template <class T>
inline std::uint64_t bitPatternOf(T value) noexcept {
    static_assert(std::is_floating_point_v<T>, "bitPatternOf 只接受浮点类型");
    if constexpr (sizeof(T) <= sizeof(std::uint32_t)) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(T));
        return static_cast<std::uint64_t>(bits);   // 低位对齐，高位补 0
    } else {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(T));
        return bits;
    }
}
//整数结果：数值本身有确定含义，直接写入 sink
template <class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
inline void consume(T value) noexcept {
    globalSink = static_cast<std::uint64_t>(value);
}

//浮点结果：按位模式写入 sink
//
//不能直接 static_cast<std::uint64_t>(浮点数)：
//  Cpp 规定"浮点转整数时，截断后的值无法用目标类型表示" 是未定义行为（UB）。
//  计算结果一旦溢出成 inf（例如 tmp 反复乘 3 放大，float 最大约 3.4e38，
//  (1.5)^219 就溢出），这个转换就是 UB：
//    - x64 上 cvttss2si 会给出"整数不定值" 0x8000000000000000（即 INT64_MIN），
//      即实测日志里的 9223372036854775808 ；
//    - 优化器有权假设 UB 不会发生，从而做出任何推理；
//    - 换成别的平台/编译器，结果完全不可预期。
//  改成搬运位模式之后：无论结果是 inf、NaN 还是普通数，写进 sink 的都是
//  **确定且可复现**的位模式，校验和相等/不等的判断始终有意义。
//
//说明：float 只占低 32 位、高位补 0；double 用满 64 位。同一个实验里两个变体
//      消费的类型一定相同，所以位模式可以直接逐位比较。
template <class T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
inline void consume(T value) noexcept {
    globalSink = bitPatternOf(value);
}

//防止编译器优化掉某变量
template <class T>
inline void doNotOptimize(const T& value) noexcept {
#if defined(_MSC_VER)//编译之前的预处理，根据编译器保留不同代码
    //读一个指向value的volatile指针，从而保证编译器不把value优化掉
    volatile const T* p = &value;
    (void)*p;                  
    _ReadWriteBarrier();
#else
    asm volatile("" : : "r,m"(value) : "memory");
#endif
}

//设置内存屏障，阻止编译器使用测试函数运行前的缓存进行优化
//放在计时区间前，隔离每次测试前后的内存访问
inline void clobberMemory() noexcept {
#if defined(_MSC_VER)//编译之前的预处理，根据编译器保留不同代码
    _ReadWriteBarrier();
#else
    asm volatile("" ::: "memory");
#endif
}

}// namespace bench

