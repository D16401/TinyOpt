#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plot_raw.py —— 把 TinyOpt 的 JSON 原始数据画成报告用图

数据来源（默认）：
    release/raw_data/<时间戳>/*.json      -> 标签 Release
    debug/main/raw_data/<时间戳>/*.json   -> 标签 Debug
    每个配置**只取最新的 3 轮**（--runs 可改，--runs 0 = 全取），所以反复跑实验后，
    图默认只反映最近一次完整实验；同时兼容旧版平铺布局（JSON 直接放在根目录下）。

输出：
    figs/<本次时间戳>/<图名>.png

画哪些图（--only 的编号 = 下面的序号 = 文件名里的 N = FIGS 里的键）：
    1  效应森林图 + 噪声带         summary_1_forest.png
    2  Release vs Debug 对照        summary_2_cfg_compare.png
    3  判定稳定性矩阵               summary_3_stability.png
    4  分辨率估计的跨轮稳定性       summary_4_resolution.png
    5  逐轮配对差（每次运行一个子图） <配置>_<实验>.png   ← 共 16 张，没有编号

（实验 5 的模型拟合图已按用户要求整体删除：那些效应与装置噪声同量级，拟合结论不可信。
  实验 5 的数据本身仍会出现在森林图、稳定性矩阵、配置对照、分辨率与逐轮图里。）

用法：
    python figs/plot_raw.py                       # 默认：两个配置各取最新 3 轮
    python figs/plot_raw.py --runs 5              # 取最新 5 轮
    python figs/plot_raw.py --only 2 5            # 只画编号 2、5 的图
    python figs/plot_raw.py --data "Release=release/raw_data" --data "Debug=debug/main/raw_data"
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import numpy as np
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.text import Text

# ---------------------------------------------------------------- 全局外观
plt.rcParams["font.sans-serif"] = ["Microsoft YaHei", "SimHei", "SimSun", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False          # 否则负号显示成方块
plt.rcParams["figure.dpi"] = 110
plt.rcParams["savefig.bbox"] = "tight"
plt.rcParams["axes.grid"] = True
plt.rcParams["grid.linestyle"] = ":"
plt.rcParams["grid.alpha"] = 0.4
plt.rcParams["hatch.linewidth"] = 1.8      # 稳定性矩阵里用斜纹标「装置自检未通过」，太细看不清

NS_PER_US = 1000.0
C_CFG = {"Release": "#1f77b4", "Debug": "#d62728"}
C_REL = {"可靠": "#2ca02c", "边缘": "#ff7f0e", "噪声淹没效应": "#d62728", "无效": "#7f7f7f"}
MARKERS = ["o", "s", "^", "D", "v"]

# 实验的显示顺序（按文件名主体，与 main.cpp 里的 saveJson 文件名和实验编号一致）。
# 表里没有的排到最后，按字典序。
EXP_ORDER = [
    "exp1_staged",
    "exp2_short_circuit",
    "exp3_loop_merge",
    "exp4_nested_order",
    "exp5_0_zero_point",
    "exp5_1_1000x4000",
    "exp5_2_500x8000",
    "exp5_3_100x40000",
]


# ---------------------------------------------------------------- 数据层
@dataclass
class Rec:
    """一条记录 = 某个配置、某次运行、某个实验的一份 JSON。"""

    cfg: str                 # Release / Debug
    stamp: str               # 该次运行的时间戳（raw_data 子目录名；平铺布局时为空）
    exp: str                 # JSON 文件名主体
    a_name: str
    b_name: str
    rounds: int
    ratio: float
    diff_us: float           # 配对差均值 A-B
    ci_us: float             # 95%CI 半宽
    a_mean_us: float
    b_mean_us: float
    resolution_us: float     # 噪声分辨率 R
    overlap: float           # 效应区间与噪声区间的重合比例
    snr: float               # |Δ| / R
    direction: str
    reliability: str
    self_ok: bool
    checksum_ok: bool
    per_a: np.ndarray
    per_b: np.ndarray
    per_diff: np.ndarray
    per_order: np.ndarray

    @property
    def label(self) -> str:
        return f"{self.a_name} → {self.b_name}"


def _num(x, default=0.0) -> float:
    """JSON 里的非有限值会被写成 null，这里统一兜底。"""
    try:
        v = float(x)
    except (TypeError, ValueError):
        return default
    return v if np.isfinite(v) else default


def load_records(roots: list[tuple[str, str]], runs: int = 3) -> list[Rec]:
    """读入原始数据。

    runs: 每个配置只取**最新的 runs 轮**（运行目录名就是 `YYYY-MM-DD_HH-MM-SS`，
           按名字排序即按时间排序）；runs <= 0 表示全取。
           这样反复跑实验时，图默认只反映最近一次完整实验，不会被旧数据摊薄。
    """
    recs: list[Rec] = []
    for cfg, root in roots:
        rootp = Path(root)
        if not rootp.is_dir():
            print(f"[跳过] 目录不存在: {rootp}", file=sys.stderr)
            continue
        # 兼容两种布局：<root>/*.json（旧版平铺）与 <root>/<时间戳>/*.json
        flat = sorted(rootp.glob("*.json"))
        dirs = sorted(d for d in rootp.iterdir() if d.is_dir())
        chosen = dirs[-runs:] if runs > 0 else dirs
        skipped = len(dirs) - len(chosen)
        if not chosen:
            detail = "没有时间戳目录"
        elif runs > 0:
            detail = f"取最新 {len(chosen)} 轮"
        else:
            detail = f"取全部 {len(chosen)} 轮（--runs 0）"
        if skipped > 0:
            detail += f"（另有 {skipped} 轮更早的未使用；--runs 0 可全取）"
        print(f"  {cfg}: {rootp}/ -> {detail}" +
              (": " + ", ".join(d.name[11:] for d in chosen) if chosen else "") +
              (f"，另有 {len(flat)} 个平铺 JSON" if flat else ""))
        # 兼容两种布局：<root>/*.json 与 <root>/<时间戳>/*.json
        files = flat + [f for d in chosen for f in sorted(d.glob("*.json"))]
        for f in files:
            try:
                j = json.loads(f.read_text(encoding="utf-8"))
            except Exception as e:                       # noqa: BLE001
                print(f"[跳过] 解析失败 {f}: {e}", file=sys.stderr)
                continue
            A, B, C = j["A"], j["B"], j["compare"]
            NF = j.get("noiseFloor", {})
            VD = j.get("verdict", {})
            stamp = f.parent.name if f.parent != rootp else ""
            recs.append(
                Rec(
                    cfg=cfg,
                    stamp=stamp,
                    exp=f.stem,
                    a_name=A.get("name", "A"),
                    b_name=B.get("name", "B"),
                    rounds=int(C.get("pairs", len(C.get("diffPerRoundNs", [])))),
                    ratio=_num(C.get("ratio"), 1.0),
                    diff_us=_num(C.get("meanDiffNs")) / NS_PER_US,
                    ci_us=_num(C.get("ci95Ns")) / NS_PER_US,
                    a_mean_us=_num(A.get("meanNs")) / NS_PER_US,
                    b_mean_us=_num(B.get("meanNs")) / NS_PER_US,
                    resolution_us=_num(NF.get("resolutionNs")) / NS_PER_US,
                    overlap=_num(NF.get("overlapFraction")),
                    snr=_num(NF.get("snr")),
                    direction=VD.get("direction", "?"),
                    reliability=VD.get("reliability", "?"),
                    self_ok=bool(NF.get("noiseSelfCheckOk", True)),
                    checksum_ok=bool(C.get("checksumMatch", False)),
                    per_a=np.asarray(A.get("perRoundNs", []), dtype=float) / NS_PER_US,
                    per_b=np.asarray(B.get("perRoundNs", []), dtype=float) / NS_PER_US,
                    per_diff=np.asarray(C.get("diffPerRoundNs", []), dtype=float) / NS_PER_US,
                    per_order=np.asarray(C.get("orderDiagPerRoundNs", []), dtype=float) / NS_PER_US,
                )
            )
    return recs


def validate(recs: list[Rec]) -> None:
    """画图前先做一遍数据自检，缺数据只警告不中断。"""
    if not recs:
        sys.exit("没有读到任何 JSON，请检查 --data 路径。")
    probs = []
    for r in recs:
        if not r.checksum_ok:
            probs.append(f"{r.cfg}/{r.stamp}/{r.exp}: 校验和不一致")
        if len(r.per_diff) != r.rounds:
            probs.append(f"{r.cfg}/{r.stamp}/{r.exp}: 逐轮数据 {len(r.per_diff)} 个，但 pairs={r.rounds}")
        if r.rounds % 2 != 0:
            probs.append(f"{r.cfg}/{r.stamp}/{r.exp}: 轮数为奇数 {r.rounds}（交替顺序不对称）")
        # 图上「明/模」和底色是照字符串映射的，取值对不上会静默画错，所以这里查一遍
        if r.direction not in ("效应方向明确", "效应方向模糊"):
            probs.append(f"{r.cfg}/{r.stamp}/{r.exp}: 未知方向判定 {r.direction!r}")
        if r.reliability not in C_REL:
            probs.append(f"{r.cfg}/{r.stamp}/{r.exp}: 未知可靠性等级 {r.reliability!r}")
    cfgs_all = sorted({r.cfg for r in recs})
    per_cfg = "、".join(f"{c} {len({r.stamp for r in recs if r.cfg == c})} 轮" for c in cfgs_all)
    print(f"读入 {len(recs)} 份 JSON：{len(cfgs_all)} 个配置 × {len({r.exp for r in recs})} 个实验"
          f"（{per_cfg}）")
    if probs:
        print(f"[警告] {len(probs)} 条数据问题：")
        for p in probs[:10]:
            print("   -", p)
    else:
        print("数据自检通过（校验和一致、逐轮数据完整、轮数为偶数）")


# ---------------------------------------------------------------- 工具
def ordered_exps(recs: list[Rec]) -> list[str]:
    exps = {r.exp for r in recs}
    known = [e for e in EXP_ORDER if e in exps]
    unknown = sorted(exps - set(known))
    return known + unknown


def runs_of(recs: list[Rec], cfg: str, exp: str) -> list[Rec]:
    return sorted((r for r in recs if r.cfg == cfg and r.exp == exp), key=lambda r: r.stamp)


def exp_title(r: Rec) -> str:
    """用 JSON 里的变体名做标题 —— 这样与文件名编号是否错位无关。"""
    return f"{r.a_name} → {r.b_name}"


def save(fig, outdir: Path, name: str, dpi: int) -> Path:
    p = outdir / f"{name}.png"
    _TEXTS.extend(t.get_text() for t in fig.findobj(Text) if t.get_text())
    fig.savefig(p, dpi=dpi)
    plt.close(fig)
    _SAVED.append(p)
    print(f"  写出 {p.name}  ({p.stat().st_size / 1024:.0f} KB)")
    return p


# 自检 1：把图上出现过的所有字符收集起来，最后核对字体是否真的覆盖它们。
# matplotlib 缺字只会在 stderr 打一行 UserWarning，容易被忽略，所以这里显式查一遍。
_TEXTS: list[str] = []
# 自检 2：回读写出的 PNG，粗查画布有没有画空/内容挤在角落（图被画坏时最常见的两种样子）。
_SAVED: list[Path] = []


def audit_font() -> None:
    from matplotlib import font_manager
    from matplotlib.ft2font import FT2Font

    fam = list(plt.rcParams["font.sans-serif"])
    path = Path(font_manager.findfont(font_manager.FontProperties(family=fam)))
    cmap = FT2Font(str(path)).get_charmap()
    chars = sorted({c for t in _TEXTS for c in t if ord(c) > 127})
    missing = [c for c in chars if ord(c) not in cmap]
    print(f"字体自检: 解析到 {path.name}（首选 {fam[0]}）")
    print(f"  非 ASCII 字符 {len(chars)} 个，缺字 {len(missing)} 个"
          + (f" -> {' '.join(missing)}" if missing else "（中文不会显示成方块）"))


def audit_render() -> None:
    print(f"渲染自检: 共 {len(_SAVED)} 张")
    odd: list[str] = []
    for p in _SAVED:
        img = plt.imread(p)                       # H×W×4，取值 0~1
        ink = (img[..., :3] < 0.92).any(axis=2)   # 非白像素 = 画上去的东西
        h, w = ink.shape
        rows, cols = np.where(ink.any(axis=1))[0], np.where(ink.any(axis=0))[0]
        if rows.size == 0:
            odd.append(p.name)
            print(f"  {p.name}: 全白！")
            continue
        frac = ink.mean() * 100
        # 内容应当铺满画布的大部分（tight bbox 之后不该有大片空白）
        cover = min((rows[-1] - rows[0] + 1) / h, (cols[-1] - cols[0] + 1) / w) * 100
        flag = ""
        if frac < 0.5 or cover < 60:
            flag = "  <== 疑似画空/内容挤在角落"
            odd.append(p.name)
        print(f"  {p.name}: {w}×{h}, 墨迹 {frac:.1f}%, 内容铺开 {cover:.0f}%{flag}")
    print(f"  可疑 {len(odd)} 张" + (f": {' '.join(odd)}" if odd else "（画布尺寸与墨迹分布正常）"))


# ---------------------------------------------------------------- 效应森林图 + 噪声带
def fig1_forest(recs: list[Rec], outdir: Path, dpi: int) -> None:
    cfgs = sorted({r.cfg for r in recs})
    exps = ordered_exps(recs)
    fig, axes = plt.subplots(1, len(cfgs), figsize=(13.5, 0.72 * len(exps) + 2.2),
                             sharey=True, squeeze=False)
    for ax, cfg in zip(axes[0], cfgs):
        rows = [e for e in exps if runs_of(recs, cfg, e)]
        for i, exp in enumerate(rows):
            rs = runs_of(recs, cfg, exp)
            R = rs[0].resolution_us
            # 噪声带：该组自己的噪声区间 ±R（横向条，便于和效应区间直接比长短）
            if R > 0:
                ax.barh(i, 2 * R, left=-R, height=0.62,
                        color="orange", alpha=0.20, zorder=1)
            for k, r in enumerate(rs):
                off = (k - (len(rs) - 1) / 2) * 0.20
                ax.errorbar(r.diff_us, i + off, xerr=r.ci_us,
                            fmt=MARKERS[k % len(MARKERS)], ms=5, capsize=2.5,
                            color=C_CFG.get(cfg, "k"), alpha=0.9, zorder=3,
                            label=f"第 {k+1} 次运行" if i == 0 else None)
        ax.axvline(0, color="k", lw=0.9, ls=":")
        ax.set_yticks(range(len(rows)))
        ax.set_yticklabels([exp_title(runs_of(recs, cfg, e)[0]) for e in rows], fontsize=8)
        ax.set_xlabel("A − B（µs，负 = A 更快）")
        ax.set_title(cfg, fontsize=11)
        ax.set_xscale("symlog", linthresh=10)   # 覆盖 0.2µs ~ 3000µs
        ax.set_ylim(-0.7, len(rows) - 0.3)
        ax.legend(loc="lower left", fontsize=7, framealpha=0.9)
    fig.suptitle("效应与噪声带   橙带 = 该实验的噪声区间 ±R（来自自比）；点 = 各次运行的配对差均值；横线 = 95%CI",
                 fontsize=11)
    fig.tight_layout()
    save(fig, outdir, "summary_1_forest", dpi)


# ---------------------------------------------------------------- 逐轮配对差
def fig5_per_round(recs: list[Rec], outdir: Path, dpi: int) -> None:
    for cfg in sorted({r.cfg for r in recs}):
        for exp in ordered_exps([r for r in recs if r.cfg == cfg]):
            rs = runs_of(recs, cfg, exp)
            if not rs:
                continue
            n = len(rs)
            fig, axes = plt.subplots(n, 1, figsize=(9.5, 2.45 * n + 0.9),
                                     sharex=True, squeeze=False)
            for ax, r in zip(axes[:, 0], rs):
                x = np.arange(1, len(r.per_diff) + 1)
                pos = r.per_diff > 0
                ax.bar(x[~pos], r.per_diff[~pos], width=0.72, color="#1f77b4",
                       label="A 更快" if r is rs[0] else None)
                ax.bar(x[pos], r.per_diff[pos], width=0.72, color="#d62728",
                       label="B 更快" if r is rs[0] else None)
                ax.axhline(0, color="k", lw=0.9)
                ax.axhline(r.diff_us, color="k", lw=1.1, ls="--",
                           label="配对差均值" if r is rs[0] else None)
                ax.axhspan(r.diff_us - r.ci_us, r.diff_us + r.ci_us,
                           color="k", alpha=0.08)
                if r.resolution_us > 0:      # 噪声区间 ±R：落在里面的差异与噪声不可分
                    ax.axhspan(-r.resolution_us, r.resolution_us,
                               color="orange", alpha=0.13,
                               label="噪声区间 ±R" if r is rs[0] else None)
                ax.set_ylabel("配对差 (µs)", fontsize=8)
                ax.set_title(
                    f"{cfg} | 运行 {r.stamp} | B/A={r.ratio:.3f} | "
                    f"{r.direction} / {r.reliability} | 重合 {r.overlap*100:.0f}%",
                    fontsize=8.5)
                # 图例只挂在第一个子图上（其余子图无带标签图元，调用会产生警告）
                if r is rs[0]:
                    ax.legend(fontsize=7, ncol=4, loc="upper right", framealpha=0.9)
            axes[-1, 0].set_xlabel("轮次")
            axes[-1, 0].set_xlim(0.3, max(len(r.per_diff) for r in rs) + 0.7)
            fig.suptitle(f"{cfg} | {exp_title(rs[0])}   逐轮配对差（每次运行一个子图）", fontsize=11)
            fig.tight_layout()
            save(fig, outdir, f"{cfg}_{exp}", dpi)


# ---------------------------------------------------------------- Release vs Debug 对照
def fig2_cfg_compare(recs: list[Rec], outdir: Path, dpi: int) -> None:
    cfgs = sorted({r.cfg for r in recs})
    if len(cfgs) < 2:
        print("  [跳过] 只有一个配置，无需对照")
        return

    def mean_by(cfg: str, exp: str, attr: str) -> float:
        rs = runs_of(recs, cfg, exp)
        return float(np.mean([getattr(r, attr) for r in rs])) if rs else 0.0

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(13, 5.0))

    # 左：有真实效应的实验 1、2 的绝对量与百分比
    big = [e for e in EXP_ORDER if e in {r.exp for r in recs}][:2]
    xs = np.arange(len(big))
    w = 0.34
    for k, cfg in enumerate(cfgs):
        vals = [abs(mean_by(cfg, e, "diff_us")) for e in big]
        pcts = [abs(mean_by(cfg, e, "diff_us")) / max(mean_by(cfg, e, "a_mean_us"), 1e-9) * 100
                for e in big]
        b = ax1.bar(xs + (k - 0.5) * w, vals, w, color=C_CFG.get(cfg, "k"), label=cfg)
        for rect, v, p in zip(b, vals, pcts):
            ax1.annotate(f"{v:,.0f} µs\n({p:.1f}%)",
                         (rect.get_x() + rect.get_width() / 2, v),
                         textcoords="offset points", xytext=(0, 3),
                         ha="center", fontsize=8)
    ax1.set_xticks(xs)
    ax1.set_xticklabels([exp_title(runs_of(recs, cfgs[0], e)[0]) for e in big], fontsize=9)
    ax1.set_ylabel("|Δ| 绝对量 (µs)")
    ax1.set_title("实验1、2：绝对收益 vs 百分比", fontsize=10.5)
    ax1.legend(fontsize=8)

    # 右：全部实验的 Δ/R（是否超过装置噪声底）
    exps = ordered_exps(recs)
    xs2 = np.arange(len(exps))
    for k, cfg in enumerate(cfgs):
        vals = [mean_by(cfg, e, "snr") for e in exps]
        ax2.bar(xs2 + (k - 0.5) * w, vals, w, color=C_CFG.get(cfg, "k"), label=cfg)
    ax2.axhline(1.0, color="k", lw=1.1, ls="--")
    ax2.annotate("Δ = R（刚够分辨）", (len(exps) - 0.5, 1.0), textcoords="offset points",
                 xytext=(-4, 4), ha="right", fontsize=8)
    ax2.set_yscale("log")
    ax2.set_xticks(xs2)
    ax2.set_xticklabels([e.replace("exp", "").replace("_", "\n", 1) for e in exps], fontsize=7)
    ax2.set_ylabel("Δ / R  （对数轴）")
    ax2.set_title("全部实验：效应相对噪声分辨率的倍数", fontsize=10.5)
    ax2.legend(fontsize=8)

    fig.suptitle("Release 与 Debug 对照：绝对收益几乎不变时，百分比会变；反之亦然", fontsize=11)
    fig.tight_layout()
    save(fig, outdir, "summary_2_cfg_compare", dpi)


# ---------------------------------------------------------------- 判定稳定性矩阵
def fig3_stability(recs: list[Rec], outdir: Path, dpi: int) -> None:
    exps = ordered_exps(recs)
    runs = sorted({(r.cfg, r.stamp) for r in recs}, key=lambda t: (t[0], t[1]))
    fig, ax = plt.subplots(figsize=(1.05 * len(runs) + 5.5, 0.62 * len(exps) + 2.4))

    for i, exp in enumerate(exps):
        for j, (cfg, stamp) in enumerate(runs):
            hit = [r for r in recs if r.exp == exp and r.cfg == cfg and r.stamp == stamp]
            if not hit:
                ax.add_patch(plt.Rectangle((j, i), 1, 1, facecolor="#f0f0f0", edgecolor="w"))
                continue
            r = hit[0]
            face = C_REL.get(r.reliability, "#cccccc")
            ax.add_patch(plt.Rectangle((j, i), 1, 1, facecolor=face, alpha=0.85, edgecolor="w"))
            if not r.self_ok:
                # 装置自检未通过：格内打白色斜纹（细黑框在彩色格上不够显眼）
                ax.add_patch(plt.Rectangle((j, i), 1, 1, facecolor="none", edgecolor="white",
                                           lw=0.0, hatch="///", zorder=3))
            ax.text(j + 0.5, i + 0.5, "明" if r.direction == "效应方向明确" else "模",
                    ha="center", va="center", fontsize=8.5,
                    color="white", fontweight="bold")
    ax.set_xlim(0, len(runs))
    ax.set_ylim(len(exps), 0)
    ax.set_xticks(np.arange(len(runs)) + 0.5)
    ax.set_xticklabels([f"{c[:3]}\n{s[11:]}" for c, s in runs], fontsize=7.5)
    ax.set_yticks(np.arange(len(exps)) + 0.5)
    ax.set_yticklabels([exp_title(runs_of(recs, runs[0][0], e)[0])
                        if runs_of(recs, runs[0][0], e) else e for e in exps], fontsize=8)
    ax.grid(False)
    handles = [plt.Rectangle((0, 0), 1, 1, facecolor=c, alpha=0.85) for c in C_REL.values()]
    labels = list(C_REL.keys())
    handles.append(plt.Rectangle((0, 0), 1, 1, facecolor="#999999", alpha=0.85,
                                 edgecolor="white", lw=0.0, hatch="///"))
    labels.append("装置自检未通过")
    ax.legend(handles, labels, fontsize=8, ncol=5,
              loc="upper center", bbox_to_anchor=(0.5, -0.09))
    ax.set_title("判定稳定性：格内「明/模」= 方向判断，底色 = 可靠性等级，斜纹 = 装置自检未通过",
                 fontsize=10.5)
    fig.tight_layout()
    save(fig, outdir, "summary_3_stability", dpi)


# ---------------------------------------------------------------- 分辨率估计的跨轮稳定性
def fig4_resolution(recs: list[Rec], outdir: Path, dpi: int) -> None:
    cfgs = sorted({r.cfg for r in recs})
    exps = ordered_exps(recs)
    xs = np.arange(len(exps))
    w = 0.34
    fig, ax = plt.subplots(figsize=(12, 4.8))
    for k, cfg in enumerate(cfgs):
        for i, exp in enumerate(exps):
            rs = runs_of(recs, cfg, exp)
            if not rs:
                continue
            vals = [r.resolution_us for r in rs]
            x = i + (k - 0.5) * w
            ax.bar(x, float(np.mean(vals)), w * 0.85, color=C_CFG.get(cfg, "k"),
                   alpha=0.55, label=cfg if i == 0 else None)
            ax.plot([x] * len(vals), vals, "o", ms=5, color=C_CFG.get(cfg, "k"))
            if len(vals) > 1:
                ax.plot([x, x], [min(vals), max(vals)], "-", lw=1.0,
                        color=C_CFG.get(cfg, "k"))
    ax.set_xticks(xs)
    ax.set_xticklabels([e.replace("exp", "").replace("_", "\n", 1) for e in exps], fontsize=7.5)
    ax.set_ylabel("噪声分辨率 R (µs)")
    ax.set_title("噪声分辨率 R 的跨轮稳定性（柱 = 均值，点 = 各次运行）", fontsize=10.5)
    ax.legend(fontsize=8)
    fig.tight_layout()
    save(fig, outdir, "summary_4_resolution", dpi)


# ---------------------------------------------------------------- main
FIGS = {1: fig1_forest, 2: fig2_cfg_compare, 3: fig3_stability,
        4: fig4_resolution, 5: fig5_per_round}


def main() -> None:
    ap = argparse.ArgumentParser(description="把 TinyOpt 的 JSON 原始数据画成报告用图")
    ap.add_argument("--data", action="append", metavar="标签=路径",
                    help="可多次指定；默认 Release=release/raw_data 与 Debug=debug/main/raw_data")
    ap.add_argument("--out", default="figs", help="输出根目录（默认 figs）")
    ap.add_argument("--only", nargs="*", type=int, default=sorted(FIGS),
                    help=f"只画指定编号的图，可选 {sorted(FIGS)}")
    ap.add_argument("--dpi", type=int, default=150)
    ap.add_argument("--runs", type=int, default=3,
                    help="每个配置只画最新的 N 轮（默认 3，与 run_full.bat 默认轮数一致；0 = 全部）")
    args = ap.parse_args()

    if args.data:
        roots = []
        for item in args.data:
            if "=" not in item:
                sys.exit(f"--data 需要「标签=路径」格式，收到: {item}")
            label, path = item.split("=", 1)
            roots.append((label.strip(), path.strip()))
    else:
        roots = [("Release", "release/raw_data"), ("Debug", "debug/main/raw_data")]

    recs = load_records(roots, args.runs)
    validate(recs)

    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    outdir = Path(args.out) / stamp
    outdir.mkdir(parents=True, exist_ok=True)
    print(f"输出目录: {outdir}")

    for i in sorted(set(args.only)):
        if i not in FIGS:
            print(f"  [跳过] 没有编号为 {i} 的图")
            continue
        print(f"图 {i}:")
        FIGS[i](recs, outdir, args.dpi)

    audit_font()
    audit_render()
    print("完成。")


if __name__ == "__main__":
    main()
