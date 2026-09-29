#pragma once
#include <vector>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

//统计工具statistic method
namespace bench{

namespace detail {

// 计算vector内double的均值
inline double meanOf(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (double x : v) s += x;
    return s / static_cast<double>(v.size());
}

// 计算vector内double的标准差（自由度n-1）
inline double sdOf(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    const double m = meanOf(v);
    double s = 0.0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}

// 计算vector内double的中位数（按值传参仅内部排序，不修改原来的顺序）
inline double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t k = v.size() / 2;
    return (v.size() % 2) ? v[k] : 0.5 * (v[k - 1] + v[k]);
}

// 双侧 95% t 临界值（df <= 30 查表，其余用 1.96）
inline double tCrit95(int df) {
    static const double t[] = {
        12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
         2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
         2.080, 2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042
    };
    if (df < 1) return 12.706;
    if (df <= 30) return t[df - 1];
    return 1.96;
}


} //namespace detail
    
}//namespace bench