"""calculationtools 自检的期望值：按教科书公式独立手算，不调用 C++ 代码。
运行：python -I calculationtools_expected.py   （输出值已抄入 tests 用例 calculationtools.cpp::goodCases）
"""
import sys
from math import pi, atan, cos, degrees, asin

sys.stdout.reconfigure(encoding="utf-8")

rho = 7850
out = {}
out["自定义展开面积 12.5m²×3mm 重量kg"] = 12.5 * 3 / 1000 * rho
out["钢板 2000×1000×10 重量kg"] = 2000 * 1000 * 10 * 1e-9 * rho
A = pi / 4 * (114**2 - 106**2)
out["圆管 Ø114×4 截面积mm²"] = A
out["圆管 kg/m"] = A * 1e-6 * rho
out["圆管 L=6000 ×3 总重kg"] = A * 6000 * 1e-9 * rho * 3
out["方管 100×5 尖角 kg/m"] = 1900e-6 * rho
out["方管 100×5 R10 截面积"] = 1900 - (4 - pi) * (10**2 - 5**2)   # 外圆角 R=10、内圆角 R-t=5
out["矩形管 200×100×6 L3000 ×2 kg"] = (20000 - 188 * 88) * 3000 * 1e-9 * rho * 2
out["圆钢 Ø20 kg/m"] = pi * 100 * 1e-6 * rho
out["压型单板 0.8mm dev1250/eff1000 kg/m²"] = 0.8 / 1000 * rho * 1250 / 1000
out["夹芯板 kg/m²"] = 0.0005 * rho * 1.25 + 0.0005 * rho * 1.0 + 0.075 * 14
out["天沟 dev800 t2 L50m kg"] = 0.8 * 50 * 0.002 * rho
out["弧 R5000 90° 20kg/m kg"] = 5000 * pi / 2 * 20 / 1000
R = (10000**2 / 4 + 2000**2) / (2 * 2000)      # 弦长+矢高
th = 2 * asin(5000 / R)                          # 与 C++ 的 atan2 写法不同，互为校核
out["弦长10000 矢高2000: R / 圆心角° / 弧长 / ×2 重量"] = (R, degrees(th), R * th, R * th / 1000 * 20 * 2)
nv = 0.9 * 1.0 * 1 * 0.45 * 155
out["螺栓 Nv / 组 / N=300 比值"] = (nv, nv * 6, 300 / (nv * 6))
H, h, b, B = 3000, 150, 260, 1200
n = H / h; L = (n - 1) * b; Ls = (H**2 + L**2) ** 0.5
wd = (1.3 * 1.0 + 1.5 * 3.5) * B / 1000 / 2; wk = (1.0 + 3.5) * B / 1000 / 2
M = wd * L**2 / 8
d = 5 * wk * L**4 / (384 * 206000 * 17800000) / cos(atan(H / L)) ** 2
out["楼梯 L / 斜长 / 倾角 / M kN·m / σ / V / δ"] = (L, Ls, degrees(atan(H / L)), M / 1e6, M / 178000, wd * L / 2000, d)
s, q, Hp = 1500, 0.5, 1100
out["栏杆 Mr / σr / δr / P / Mp / σp / δp"] = (q*s*s/8/1e6, q*s*s/8/4000, 5*q*s**4/(384*206000*1.2e5),
                                             q*s/1000, q*s*Hp/1e6, q*s*Hp/8000, q*s*Hp**3/(3*206000*3e5))
for k, v in out.items():
    print(k, repr(v))
