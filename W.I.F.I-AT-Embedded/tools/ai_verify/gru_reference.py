#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
보드 없는 검증 #2 — 수치/알고리즘 검증.

gru_functions.c 와 '동일한' GRU(reset_after=True) 순전파를 파이썬으로 재구현하고,
components/AI/gru_weights.h 에서 실제 가중치를 로드해 다음을 확인한다:
  1) 가중치 배열 차원 무결성 (export 형상과 일치)
  2) 출력 범위 (sigmoid ∈ (0,1)), NaN/Inf 없음
  3) 결정성 (같은 입력 → 같은 출력)
  4) 민감도 (다른 입력 → 다른 출력; 정적/동적 윈도가 구분됨)

한계: 이 스크립트는 '알고리즘/가중치'를 검증한다. C 컴파일 자체는 build_check.sh(타깃 gcc)가 검증한다.
실제 낙상 정확도는 학습셋 전처리(정규화/윈도) 확정 + 실기기 필요.
"""
import re, sys, math, pathlib

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = pathlib.Path(__file__).resolve().parent
WEIGHTS_H = HERE.parent.parent / "components" / "AI" / "gru_weights.h"

INPUT_DIM, UNITS, SEQ_LEN = 64, 16, 71
EXPECT = {
    "W_z": 1024, "W_r": 1024, "W_h": 1024,
    "U_z": 256, "U_r": 256, "U_h": 256,
    "b_input_z": 16, "b_input_r": 16, "b_input_h": 16,
    "b_rec_z": 16, "b_rec_r": 16, "b_rec_h": 16,
    "dense1_w": 256, "dense1_b": 16, "dense2_w": 16, "dense2_b": 1,
}

def load_weights(path):
    txt = path.read_text(errors="replace")
    w = {}
    for name in EXPECT:
        m = re.search(r"float\s+" + re.escape(name) + r"\s*\[(\d+)\]\s*=\s*\{([^}]*)\}", txt)
        if not m:
            raise SystemExit(f"[FAIL] 배열 {name} 를 gru_weights.h 에서 못 찾음")
        vals = [float(x) for x in re.findall(r"-?\d+\.?\d*(?:[eE]-?\d+)?", m.group(2))]
        w[name] = vals
    return w

def sig(x):
    return 1.0 / (1.0 + math.exp(-x))

def gru_predict(window, w):
    """gru_functions.c 와 동일한 reset_after=True 순전파."""
    h = [0.0] * UNITS
    for t in range(SEQ_LEN):
        x = window[t]
        xz = [w["b_input_z"][j] + sum(x[i] * w["W_z"][i*UNITS+j] for i in range(INPUT_DIM)) for j in range(UNITS)]
        xr = [w["b_input_r"][j] + sum(x[i] * w["W_r"][i*UNITS+j] for i in range(INPUT_DIM)) for j in range(UNITS)]
        xh = [w["b_input_h"][j] + sum(x[i] * w["W_h"][i*UNITS+j] for i in range(INPUT_DIM)) for j in range(UNITS)]
        rz = [w["b_rec_z"][j] + sum(h[i] * w["U_z"][i*UNITS+j] for i in range(UNITS)) for j in range(UNITS)]
        rr = [w["b_rec_r"][j] + sum(h[i] * w["U_r"][i*UNITS+j] for i in range(UNITS)) for j in range(UNITS)]
        rh = [w["b_rec_h"][j] + sum(h[i] * w["U_h"][i*UNITS+j] for i in range(UNITS)) for j in range(UNITS)]
        nh = [0.0] * UNITS
        for j in range(UNITS):
            z = sig(xz[j] + rz[j])
            r = sig(xr[j] + rr[j])
            hh = math.tanh(xh[j] + r * rh[j])   # reset_after: r 는 bias 합산 후 적용
            nh[j] = z * h[j] + (1.0 - z) * hh
        h = nh
    d1 = [max(0.0, w["dense1_b"][j] + sum(h[i] * w["dense1_w"][i*16+j] for i in range(UNITS))) for j in range(16)]
    s = w["dense2_b"][0] + sum(d1[i] * w["dense2_w"][i] for i in range(16))
    return sig(s)

def prng(seed):
    s = seed & 0xffffffff
    while True:
        s = (1103515245 * s + 12345) & 0x7fffffff
        yield s / 0x7fffffff

def make_window(kind, seed=1):
    g = prng(seed)
    if kind == "zero":
        return [[0.0]*INPUT_DIM for _ in range(SEQ_LEN)]
    if kind == "static":   # 사람 정지: 프레임 간 변화 거의 없음
        base = [10.0 + 2.0*next(g) for _ in range(INPUT_DIM)]
        return [[b + 0.01*next(g) for b in base] for _ in range(SEQ_LEN)]
    if kind == "dynamic":  # 큰 움직임(낙상 유사): 프레임 간 진폭 급변
        return [[5.0 + 20.0*next(g) for _ in range(INPUT_DIM)] for _ in range(SEQ_LEN)]
    raise ValueError(kind)

def main():
    print(f"weights: {WEIGHTS_H}")
    w = load_weights(WEIGHTS_H)
    fails = []

    # 1) 차원 무결성
    for name, n in EXPECT.items():
        got = len(w[name])
        tag = "ok" if got == n else "FAIL"
        if got != n: fails.append(f"{name} len {got}!={n}")
    print(f"[1] 차원 무결성: {'PASS' if not fails else 'FAIL'} (배열 {len(EXPECT)}개)")

    # 2) 범위 + NaN/Inf
    outs = {k: gru_predict(make_window(k, 7), w) for k in ("zero", "static", "dynamic")}
    for k, o in outs.items():
        if not (0.0 < o < 1.0) or math.isnan(o) or math.isinf(o):
            fails.append(f"out[{k}]={o} 범위이탈")
    print(f"[2] 출력범위(0,1): {'PASS' if all(0<o<1 for o in outs.values()) else 'FAIL'}  " +
          " ".join(f"{k}={o:.4f}" for k, o in outs.items()))

    # 3) 결정성
    a = gru_predict(make_window("dynamic", 42), w)
    b = gru_predict(make_window("dynamic", 42), w)
    det = (a == b)
    if not det: fails.append("결정성 위반")
    print(f"[3] 결정성: {'PASS' if det else 'FAIL'} ({a:.6f} == {b:.6f})")

    # 4) 민감도 (정적 vs 동적 윈도가 다른 출력)
    sens = abs(outs["static"] - outs["dynamic"]) > 1e-4
    if not sens: fails.append("민감도 없음(입력에 반응 안 함)")
    print(f"[4] 민감도(static≠dynamic): {'PASS' if sens else 'FAIL'} " +
          f"|Δ|={abs(outs['static']-outs['dynamic']):.4f}")

    print("-" * 40)
    if fails:
        print("결과: FAIL\n  - " + "\n  - ".join(fails))
        sys.exit(1)
    print("결과: PASS — 알고리즘/가중치 수치 검증 통과")
    print("주의: 출력 극성(정상 vs 낙상)과 정규화는 학습 파이프라인 확정 필요(정확도와 별개).")

if __name__ == "__main__":
    main()
