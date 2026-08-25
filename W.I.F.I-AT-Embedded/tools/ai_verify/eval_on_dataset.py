#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
보드 없는 테스트 #4 — 실제 CSI 데이터셋에서 판별력/극성 확인 (근사).

gru_weights.h 의 가중치로, csi_data/csi_dataset_clean.csv 의 정상/낙상 샘플을
GRU(reset_after)에 넣어 출력이 두 클래스를 구분하는지 본다.

⚠️ 근사인 이유: 학습 텐서는 (88,71,64)인데 현재 CSV 는 72샘플·최대 70프레임.
   학습 시 패딩/윈도잉/정규화 방식을 알 수 없어, 여기선 '마지막 프레임 반복'으로
   71 로 맞추고 raw 그대로 넣는다. 따라서 결과는 '경향/극성 힌트'이지 확정 정확도가 아님.
"""
import csv, os, re, sys, pathlib
import numpy as np

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = pathlib.Path(__file__).resolve().parent
WEIGHTS_H = HERE.parent.parent / "components" / "AI" / "gru_weights.h"
# 프로젝트 루트의 csi_data 를 찾는다(레포 밖일 수 있어 여러 후보 탐색)
CANDIDATES = [
    pathlib.Path("C:/project/wify/csi_data/csi_dataset_clean.csv"),
    pathlib.Path("C:/project/wify/csi_data/csi_dataset.csv"),
]
INPUT_DIM, UNITS, SEQ_LEN = 64, 16, 71
NAMES = ["W_z","W_r","W_h","U_z","U_r","U_h","b_input_z","b_input_r","b_input_h",
         "b_rec_z","b_rec_r","b_rec_h","dense1_w","dense1_b","dense2_w","dense2_b"]

def load_weights(path):
    txt = path.read_text(errors="replace"); w={}
    for n in NAMES:
        m = re.search(r"float\s+"+re.escape(n)+r"\s*\[(\d+)\]\s*=\s*\{([^}]*)\}", txt)
        w[n] = np.array([float(x) for x in re.findall(r"-?\d+\.?\d*(?:[eE]-?\d+)?", m.group(2))], dtype=np.float64)
    w["W_z"]=w["W_z"].reshape(INPUT_DIM,UNITS); w["W_r"]=w["W_r"].reshape(INPUT_DIM,UNITS); w["W_h"]=w["W_h"].reshape(INPUT_DIM,UNITS)
    w["U_z"]=w["U_z"].reshape(UNITS,UNITS);     w["U_r"]=w["U_r"].reshape(UNITS,UNITS);     w["U_h"]=w["U_h"].reshape(UNITS,UNITS)
    w["dense1_w"]=w["dense1_w"].reshape(UNITS,16)
    return w

def sig(x): return 1.0/(1.0+np.exp(-x))

def gru_predict(win, w):
    """gru_functions.c 와 동일한 reset_after=True 순전파. win: (71,64)"""
    h = np.zeros(UNITS)
    for t in range(SEQ_LEN):
        x = win[t]
        xz = x@w["W_z"]+w["b_input_z"]; xr = x@w["W_r"]+w["b_input_r"]; xh = x@w["W_h"]+w["b_input_h"]
        rz = h@w["U_z"]+w["b_rec_z"];   rr = h@w["U_r"]+w["b_rec_r"];   rh = h@w["U_h"]+w["b_rec_h"]
        z = sig(xz+rz); r = sig(xr+rr); hh = np.tanh(xh + r*rh)
        h = z*h + (1.0-z)*hh
    d1 = np.maximum(0.0, h@w["dense1_w"]+w["dense1_b"])
    return float(sig(d1@w["dense2_w"]+w["dense2_b"]))

def load_samples(path):
    samples = {}
    with open(path, encoding="utf-8") as fh:
        r = csv.reader(fh); next(r)
        for row in r:
            if not row or not row[0].isdigit(): continue
            sid=int(row[0]); label=row[1]
            vals=[]
            for c in row[4:4+INPUT_DIM]:
                try: vals.append(float(c))
                except: vals.append(0.0)
            while len(vals)<INPUT_DIM: vals.append(0.0)
            samples.setdefault(sid, {"label":label, "frames":[]})["frames"].append(vals[:INPUT_DIM])
    return samples

def to_window(frames):
    a = np.array(frames, dtype=np.float64)
    if len(a) >= SEQ_LEN: return a[:SEQ_LEN]
    pad = np.repeat(a[-1:], SEQ_LEN-len(a), axis=0)   # 마지막 프레임 반복
    return np.vstack([a, pad])

def main():
    ds = next((p for p in CANDIDATES if p.exists()), None)
    if ds is None: sys.exit("csi_dataset(_clean).csv 없음")
    print(f"weights : {WEIGHTS_H.name}")
    print(f"dataset : {ds}")
    w = load_weights(WEIGHTS_H)
    samples = load_samples(ds)

    outs = {"normal":[], "fall":[]}
    for sid, s in sorted(samples.items()):
        if s["label"] not in outs: continue
        outs[s["label"]].append(gru_predict(to_window(s["frames"]), w))
    n = np.array(outs["normal"]); f = np.array(outs["fall"])
    print(f"\n샘플수  normal={len(n)}  fall={len(f)}")
    print(f"출력평균 normal={n.mean():.4f} (±{n.std():.3f})   fall={f.mean():.4f} (±{f.std():.3f})")

    # 극성 추정: 출력이 높을수록 어느 클래스인가?
    if n.mean() > f.mean():
        polarity = "출력 높음 = normal  → GRU_OUTPUT_IS_P_NORMAL=1 (현재 코드와 일치)"
    else:
        polarity = "출력 높음 = fall    → GRU_OUTPUT_IS_P_NORMAL=0 로 바꿔야 함(현재와 반대!)"

    # 최적 임계값에서의 분리 정확도(어느 방향이든 최고를 택함)
    allv = np.concatenate([n,f]); best=0.0; bt=0.5
    for th in np.linspace(allv.min(), allv.max(), 101):
        acc1 = (np.mean(n< th)*len(n)+np.mean(f>=th)*len(f))/(len(n)+len(f))  # 높음=fall
        acc2 = (np.mean(n>=th)*len(n)+np.mean(f< th)*len(f))/(len(n)+len(f))  # 높음=normal
        a = max(acc1,acc2)
        if a>best: best=a; bt=th
    # AUC(순위 기반, 방향 무관 분리도)
    from itertools import product
    wins = sum(1 for a,b in product(f,n) if a>b) + 0.5*sum(1 for a,b in product(f,n) if a==b)
    auc = wins/(len(f)*len(n)); auc = max(auc, 1-auc)

    print(f"분리도 AUC(방향무관) = {auc:.3f}   (0.5=랜덤, 1.0=완벽)")
    print(f"최적 임계값 정확도   = {best*100:.1f}%  @ th={bt:.3f}")
    print(f"극성 추정: {polarity}")
    print("\n주의(근사): 학습 텐서(88,71,64) ≠ 현재 CSV(72샘플,≤70프레임).")
    print("  패딩/정규화 방식 미확정 → 위 수치는 '경향 힌트'. 확정은 학습 전처리 정합 + 실기기 필요.")

if __name__ == "__main__":
    main()
