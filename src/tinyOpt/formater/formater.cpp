#include "formater.h"


namespace bench{

//顺序偏差判定：残留的"后测-先测"偏差会不会污染效应结论。
//  基准取"效应量级"与"噪声分辨率"中较大的一个 ——
//  只看"偏差/效应"是不够的：当效应本身接近 0（例如零点自检、或两个变体工作量本来就相同），
//  这个比值会被除零放大，0.3 µs 的偏差也会被算成"大幅波动"。
//  再用偏差自己的 95%CI 判断它是"真实存在"还是"统计上还没证实"：
//    [可忽略]   占比<=10%
//    [轻微]     占比<=30%
//    [显著偏差]  占比>30% 且偏差均值的95%CI不跨0：偏差存在且方向明确，足以干扰结论
//    [大幅波动]  占比>30% 但偏差均值的95%CI跨0：幅度不小但各轮之间波动大，均值与0不可区分
OrderBiasLevel orderBiasLevel(const Comparison& c) {
    const double bias   = std::fabs(c.orderBiasNs); //平均顺序偏差（绝对值）
    const double effect = std::fabs(c.meanDiffNs);  //平均效应（绝对值）
    const double base   = std::max(effect, c.resolutionNs);   //取两者较大者当基准
    if (base <= 0.0) return OrderBiasLevel::Unknown;

    const double rel         = bias / base;         //顺序偏差相对基准的占比
    const bool   established = (c.orderBiasCi95Ns > 0.0) && (bias > c.orderBiasCi95Ns);

    if (rel <= 0.10) return OrderBiasLevel::Negligible;
    if (rel <= 0.30) return OrderBiasLevel::Slight;
    return established ? OrderBiasLevel::Significant : OrderBiasLevel::Volatile;
}
    
}