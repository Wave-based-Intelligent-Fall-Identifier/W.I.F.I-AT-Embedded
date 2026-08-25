#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fall_flask.py — CSI 낙상 모니터 (호스트 추론, 보드엔 안 올림)

펌웨어(components/AI/espAI.c)의 추론 경로를 PC에서 '그대로' 재현한다.
  - UART(COM) 또는 CSV 로 흘러나오는 CSI I/Q 라인을 읽어 amp[64] 계산
  - 71프레임 롤링 윈도를 채우고 FALL_INFER_STRIDE(16)프레임마다 GRU 추론
  - out = P(normal)  →  p_fall = 1 - out  (GRU_OUTPUT_IS_P_NORMAL=1)
  - p_fall >= 0.60 → 낙상(DAN) / >=0.40 → 의심(WARN) / else 정상(NOR)
  - 가중치는 components/AI/gru_weights.h 에서 직접 로드(단일 진실원천)

펌웨어와 다른 점(의도적): baseline/PIR 캘리브레이션 게이트는 없다.
  호스트 도구는 '모델 자체'를 눈으로 보기 위한 것이므로 들어오는 프레임을 바로 추론한다.

의존성:  pip install flask pyserial numpy
실행:    python tools/ai_verify/fall_flask.py
         http://127.0.0.1:5000
"""

import math
import os
import re
import pathlib
import threading

import numpy as np
from flask import Flask, jsonify, request

# ---------------------------------------------------------------------------
# 형상/임계값 — 펌웨어와 1:1 (gru_functions.h / espAI.c)
# ---------------------------------------------------------------------------
INPUT_DIM = 64        # GRU_INPUT_DIM
UNITS = 16            # gru_functions.c UNITS
SEQ_LEN = 71          # GRU_SEQ_LEN
INFER_STRIDE = 16     # FALL_INFER_STRIDE
PFALL_DANGER = 0.60   # GRU_PFALL_DANGER
PFALL_WARNING = 0.40  # GRU_PFALL_WARNING
OUTPUT_IS_P_NORMAL = True  # GRU_OUTPUT_IS_P_NORMAL=1  → p_fall = 1-out

HERE = pathlib.Path(__file__).resolve().parent
WEIGHTS_H = HERE.parent.parent / "components" / "AI" / "gru_weights.h"

# ---------------------------------------------------------------------------
# gru_weights.h 로드 → numpy (gru_reference.py 와 동일한 배열 이름/차원)
# ---------------------------------------------------------------------------
_EXPECT = {
    "W_z": (INPUT_DIM, UNITS), "W_r": (INPUT_DIM, UNITS), "W_h": (INPUT_DIM, UNITS),
    "U_z": (UNITS, UNITS), "U_r": (UNITS, UNITS), "U_h": (UNITS, UNITS),
    "b_input_z": (UNITS,), "b_input_r": (UNITS,), "b_input_h": (UNITS,),
    "b_rec_z": (UNITS,), "b_rec_r": (UNITS,), "b_rec_h": (UNITS,),
    "dense1_w": (UNITS, 16), "dense1_b": (16,),
    "dense2_w": (16,), "dense2_b": (1,),
}


def load_weights(path):
    txt = pathlib.Path(path).read_text(errors="replace")
    w = {}
    for name, shape in _EXPECT.items():
        m = re.search(r"float\s+" + re.escape(name) + r"\s*\[(\d+)\]\s*=\s*\{([^}]*)\}", txt)
        if not m:
            raise SystemExit(f"[FAIL] 배열 {name} 를 {path} 에서 못 찾음")
        vals = [float(x) for x in re.findall(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?", m.group(2))]
        arr = np.asarray(vals, dtype=np.float32)
        if arr.size != int(np.prod(shape)):
            raise SystemExit(f"[FAIL] {name} 크기 {arr.size} != 기대 {int(np.prod(shape))}")
        w[name] = arr.reshape(shape)
    return w


def _sigmoid(x):
    return 1.0 / (1.0 + np.exp(-x))


class GruModel:
    """gru_functions.c 와 수식 동일한 reset_after=True GRU + Dense2단 (numpy 벡터화)."""

    def __init__(self, weights):
        self.w = weights

    def predict(self, window):
        """window: (SEQ_LEN, INPUT_DIM) → P(normal) ∈ (0,1)."""
        w = self.w
        X = np.asarray(window, dtype=np.float32)           # (71,64)
        # 입력 게이트 사전계산: 전 타임스텝 한 번에 (71,16)
        xz = X @ w["W_z"] + w["b_input_z"]
        xr = X @ w["W_r"] + w["b_input_r"]
        xh = X @ w["W_h"] + w["b_input_h"]
        h = np.zeros(UNITS, dtype=np.float32)
        for t in range(SEQ_LEN):
            rz = h @ w["U_z"] + w["b_rec_z"]
            rr = h @ w["U_r"] + w["b_rec_r"]
            rh = h @ w["U_h"] + w["b_rec_h"]
            z = _sigmoid(xz[t] + rz)
            r = _sigmoid(xr[t] + rr)
            hh = np.tanh(xh[t] + r * rh)   # reset_after: r 는 bias 합산 후 적용
            h = z * h + (1.0 - z) * hh
        d1 = np.maximum(0.0, h @ w["dense1_w"] + w["dense1_b"])   # relu
        s = float(d1 @ w["dense2_w"] + w["dense2_b"][0])
        return float(_sigmoid(s))


def judge(p_fall):
    """(라벨, 코드) — 펌웨어 pfall_to_judg 와 동일."""
    if p_fall >= PFALL_DANGER:
        return "FALL", "DAN"
    if p_fall >= PFALL_WARNING:
        return "WARNING", "WARN"
    return "NORMAL", "NOR"


# ---------------------------------------------------------------------------
# 라인 → amp[64] 파서 (graph_flask.py 규칙과 동일 + 64폭 정합)
# ---------------------------------------------------------------------------
LOG_PREFIX_RE = re.compile(r"^[IWEDV]\s*\(\d+\)\s*[\w\-/.]+:\s*")
NUM_RE = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")


def line_to_amp(line, interpret="iq", min_nums=16):
    """UART/CSV 한 줄 → amp 리스트(가변폭). 데이터 아니면 None."""
    line = LOG_PREFIX_RE.sub("", line.strip())
    if not line:
        return None
    if "[" in line and "]" in line:
        line = line[line.index("[") + 1: line.rindex("]")]
    nums = [float(x) for x in NUM_RE.findall(line)]
    if len(nums) < min_nums:
        return None
    if interpret == "amp":
        return nums
    amp = []                         # iq: [im, re, im, re, ...] → sqrt(re^2+im^2)
    for i in range(len(nums) // 2):
        im, re_ = nums[2 * i], nums[2 * i + 1]
        amp.append(math.sqrt(re_ * re_ + im * im))
    return amp


def fit_to_model_width(amp):
    """펌웨어 espAI.c 와 동일: 앞 64개만 취하고 부족분은 0으로 패딩."""
    frame = np.zeros(INPUT_DIM, dtype=np.float32)
    n = min(len(amp), INPUT_DIM)
    frame[:n] = amp[:n]
    return frame


# ---------------------------------------------------------------------------
# 리더 + 추론 상태
# ---------------------------------------------------------------------------
class Monitor:
    def __init__(self, model):
        self.model = model
        self.lock = threading.Lock()
        self.stop_evt = threading.Event()
        self.thread = None

        self.mode = "serial"
        self.cfg = {
            "port": "COM13", "baud": 115200,
            "file": "csi_capture.csv", "fps": 20,
            "interpret": "iq", "min_nums": 16,
        }

        # 롤링 윈도 (항상 시간 오름차순, 펌웨어 s_win 과 동일 규칙)
        self.win = np.zeros((SEQ_LEN, INPUT_DIM), dtype=np.float32)
        self.win_count = 0
        self.infer_counter = 0

        self.latest_amp = []      # 최신 프레임(그래프용, 원폭 유지)
        self.idx = 0
        self.frames = 0
        self.p_fall = 0.0
        self.out = 0.0            # 모델 원출력 P(normal)
        self.label = "NORMAL"
        self.code = "NOR"
        self.n_sub = 0           # 최신 프레임 서브캐리어 수(정합 확인용)
        self.status = "idle"
        self.error = ""

    # --- 제어 ---
    def apply(self, mode=None, cfg=None):
        self.stop()
        if mode in ("serial", "file"):
            self.mode = mode
        if cfg:
            for k, v in cfg.items():
                if k in self.cfg and v is not None and v != "":
                    self.cfg[k] = type(self.cfg[k])(v)
        with self.lock:              # 소스 바꾸면 윈도 초기화
            self.win_count = 0
            self.infer_counter = 0
        self.start()

    def start(self):
        self.stop_evt.clear()
        target = self._serial_loop if self.mode == "serial" else self._file_loop
        self.thread = threading.Thread(target=target, daemon=True)
        self.thread.start()

    def stop(self):
        self.stop_evt.set()
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=2)
        self.thread = None
        self.stop_evt.clear()

    # --- 프레임 수신 → 윈도 push → (stride마다) 추론 ---
    def _feed(self, amp):
        frame = fit_to_model_width(amp)
        with self.lock:
            if self.win_count < SEQ_LEN:
                self.win[self.win_count] = frame
                self.win_count += 1
            else:
                self.win[:-1] = self.win[1:]
                self.win[-1] = frame
            self.latest_amp = list(amp)
            self.n_sub = len(amp)
            self.idx += 1
            self.frames += 1
            self.infer_counter += 1
            do_infer = (self.infer_counter >= INFER_STRIDE and self.win_count >= SEQ_LEN)
            if do_infer:
                self.infer_counter = 0
                win = self.win.copy()   # 락 밖에서 추론하려 스냅샷
        if do_infer:
            out = self.model.predict(win)
            p_fall = (1.0 - out) if OUTPUT_IS_P_NORMAL else out
            label, code = judge(p_fall)
            with self.lock:
                self.out = out
                self.p_fall = p_fall
                self.label = label
                self.code = code

    def snapshot(self):
        with self.lock:
            return {
                "idx": self.idx, "frames": self.frames,
                "amp": self.latest_amp, "n": len(self.latest_amp), "n_sub": self.n_sub,
                "win_count": self.win_count, "seq_len": SEQ_LEN,
                "p_fall": round(self.p_fall, 4), "p_normal": round(self.out, 4),
                "label": self.label, "code": self.code,
                "ready": self.win_count >= SEQ_LEN,
                "mode": self.mode, "status": self.status, "error": self.error,
                "cfg": dict(self.cfg),
                "thresholds": {"danger": PFALL_DANGER, "warning": PFALL_WARNING},
            }

    # --- 시리얼 소스 ---
    def _serial_loop(self):
        try:
            import serial
        except ImportError:
            self.status, self.error = "error", "pyserial 미설치: pip install pyserial"
            return
        try:
            ser = serial.Serial(self.cfg["port"], self.cfg["baud"], timeout=1)
        except Exception as e:  # noqa: BLE001
            self.status, self.error = "error", f"시리얼 열기 실패: {e}"
            return
        self.status, self.error = "running", ""
        try:
            while not self.stop_evt.is_set():
                raw = ser.readline()
                if not raw:
                    continue
                amp = line_to_amp(raw.decode("utf-8", "replace"),
                                  self.cfg["interpret"], self.cfg["min_nums"])
                if amp:
                    self._feed(amp)
        except Exception as e:  # noqa: BLE001
            self.status, self.error = "error", f"시리얼 읽기 오류: {e}"
        finally:
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
            if self.status == "running":
                self.status = "stopped"

    # --- CSV 재생 소스 (라이브 기기 없이 테스트) ---
    def _file_loop(self):
        path = self.cfg["file"]
        if not os.path.exists(path):
            self.status, self.error = "error", f"파일 없음: {os.path.abspath(path)}"
            return
        self.status, self.error = "running", ""
        delay = 1.0 / max(self.cfg["fps"], 0.1)
        try:
            while not self.stop_evt.is_set():
                with open(path, "r", encoding="utf-8", errors="replace") as f:
                    for line in f:
                        if self.stop_evt.is_set():
                            break
                        amp = line_to_amp(line, self.cfg["interpret"], self.cfg["min_nums"])
                        if amp:
                            self._feed(amp)
                            if self.stop_evt.wait(delay):
                                break
                if self.stop_evt.wait(0.5):
                    break
        except Exception as e:  # noqa: BLE001
            self.status, self.error = "error", f"파일 재생 오류: {e}"
        finally:
            if self.status == "running":
                self.status = "stopped"


MODEL = GruModel(load_weights(WEIGHTS_H))
monitor = Monitor(MODEL)

# ---------------------------------------------------------------------------
# 웹 UI — 모던/심플/깔끔 (단일 파일 인라인)
# ---------------------------------------------------------------------------
PAGE = r"""<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CSI 낙상 모니터</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
  :root{
    --bg:#0b0f14; --panel:#111820; --panel2:#0e141b; --line:#1e2833;
    --text:#e7edf3; --muted:#8a99a8; --accent:#3b82f6;
    --nor:#22c55e; --warn:#f59e0b; --fall:#ef4444;
    --radius:16px; --shadow:0 1px 0 rgba(255,255,255,.03),0 12px 32px rgba(0,0,0,.35);
  }
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--text);
       font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",system-ui,sans-serif;
       -webkit-font-smoothing:antialiased;}
  header{display:flex;align-items:center;gap:14px;padding:16px 22px;flex-wrap:wrap;
         border-bottom:1px solid var(--line);background:var(--panel2);position:sticky;top:0;z-index:5;}
  .brand{font-size:15px;font-weight:700;letter-spacing:.2px;margin-right:6px;display:flex;gap:8px;align-items:center;}
  .brand .dot{width:9px;height:9px;border-radius:50%;background:var(--muted);box-shadow:0 0 0 4px rgba(138,153,168,.12);}
  .brand .dot.live{background:var(--nor);box-shadow:0 0 0 4px rgba(34,197,94,.15);}
  .seg{display:flex;background:var(--panel);border:1px solid var(--line);border-radius:10px;overflow:hidden}
  .seg button{border:0;background:transparent;color:var(--muted);padding:7px 14px;font-size:13px;cursor:pointer;font-weight:600}
  .seg button.active{background:var(--accent);color:#fff}
  .field{display:flex;flex-direction:column;gap:3px}
  .field label{font-size:10px;text-transform:uppercase;letter-spacing:.6px;color:var(--muted)}
  input{background:var(--panel);color:var(--text);border:1px solid var(--line);border-radius:9px;padding:7px 10px;font-size:13px;width:96px}
  input.wide{width:190px}
  .btn{background:var(--accent);color:#fff;border:0;border-radius:10px;padding:9px 16px;font-size:13px;font-weight:700;cursor:pointer}
  .btn.ghost{background:var(--panel);color:var(--text);border:1px solid var(--line)}
  .spacer{flex:1}
  .pill{font-size:12px;color:var(--muted);display:flex;gap:6px;align-items:center}
  .pill b{color:var(--text);font-variant-numeric:tabular-nums;font-weight:600}
  main{max-width:1180px;margin:0 auto;padding:22px;display:grid;gap:18px;grid-template-columns:360px 1fr}
  @media(max-width:880px){main{grid-template-columns:1fr}}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:var(--radius);box-shadow:var(--shadow)}
  .card .hd{padding:14px 18px 0;font-size:12px;color:var(--muted);font-weight:600;letter-spacing:.3px}
  .card .bd{padding:16px 18px 18px}
  /* 상태 히어로 */
  .hero{grid-row:span 1;display:flex;flex-direction:column;justify-content:space-between}
  .hero .state{display:flex;align-items:center;gap:16px}
  .hero .glyph{width:64px;height:64px;border-radius:18px;display:grid;place-items:center;font-size:30px;flex:none;
               background:rgba(34,197,94,.12);color:var(--nor);transition:.25s}
  .hero .label{font-size:30px;font-weight:800;letter-spacing:-.5px;line-height:1}
  .hero .sub{font-size:13px;color:var(--muted);margin-top:6px}
  .hero.warn .glyph{background:rgba(245,158,11,.14);color:var(--warn)}
  .hero.fall .glyph{background:rgba(239,68,68,.15);color:var(--fall)}
  .hero.warn .label{color:var(--warn)} .hero.fall .label{color:var(--fall)}
  .hero.fall{animation:pulse 1s ease-in-out infinite}
  @keyframes pulse{0%,100%{box-shadow:var(--shadow)}50%{box-shadow:0 0 0 2px rgba(239,68,68,.5),var(--shadow)}}
  /* 게이지 */
  .gauge{margin-top:20px}
  .gauge .row{display:flex;justify-content:space-between;font-size:12px;color:var(--muted);margin-bottom:7px}
  .gauge .row b{color:var(--text);font-variant-numeric:tabular-nums;font-size:15px}
  .track{position:relative;height:12px;border-radius:8px;background:var(--panel2);overflow:hidden;border:1px solid var(--line)}
  .track .fill{position:absolute;inset:0 auto 0 0;border-radius:8px;background:linear-gradient(90deg,#22c55e,#f59e0b 55%,#ef4444);transition:width .3s}
  .ticks{position:relative;height:14px;margin-top:4px}
  .ticks span{position:absolute;transform:translateX(-50%);font-size:9px;color:var(--muted)}
  .ticks i{position:absolute;top:-6px;width:1px;height:6px;background:var(--line);transform:translateX(-50%)}
  .meta{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:20px}
  .meta .box{background:var(--panel2);border:1px solid var(--line);border-radius:11px;padding:11px 13px}
  .meta .box .k{font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:.5px}
  .meta .box .v{font-size:17px;font-weight:700;margin-top:3px;font-variant-numeric:tabular-nums}
  canvas#spectrum{width:100%!important}
  #waterfall{width:100%;height:240px;image-rendering:pixelated;background:#05080b;border-radius:10px;border:1px solid var(--line)}
  .warnbar{margin:0 22px;max-width:1180px;margin-left:auto;margin-right:auto;padding:0 22px}
  .note{background:rgba(245,158,11,.08);border:1px solid rgba(245,158,11,.25);color:#f6c96b;border-radius:12px;
        padding:10px 14px;font-size:12.5px;margin:0 auto;max-width:1180px}
</style>
</head>
<body>
<header>
  <div class="brand"><span class="dot" id="livedot"></span>CSI 낙상 모니터</div>
  <div class="seg">
    <button id="btnSerial" class="active">Serial · UART</button>
    <button id="btnFile">File · CSV</button>
  </div>
  <div class="field"><label>port</label><input id="port" value="COM13"></div>
  <div class="field"><label>baud</label><input id="baud" value="115200"></div>
  <div class="field"><label>csv file</label><input id="file" class="wide" value="csi_capture.csv"></div>
  <div class="field"><label>fps</label><input id="fps" value="20"></div>
  <div class="seg" title="숫자 해석">
    <button id="btnIq" class="active">I/Q→amp</button>
    <button id="btnAmp">amp</button>
  </div>
  <button class="btn" id="apply">연결 / 적용</button>
  <div class="spacer"></div>
  <div class="pill">win <b id="winfill">0/71</b></div>
  <div class="pill">frames <b id="frames">0</b></div>
  <div class="pill">sub <b id="nsub">0</b></div>
</header>

<div style="max-width:1180px;margin:14px auto 0;padding:0 22px">
  <div class="note" id="note" style="display:none"></div>
</div>

<main>
  <!-- 상태 히어로 -->
  <section class="card hero" id="hero">
    <div class="bd">
      <div class="state">
        <div class="glyph" id="glyph">🧍</div>
        <div>
          <div class="label" id="statelabel">NORMAL</div>
          <div class="sub" id="statesub">정상 — 움직임 안정</div>
        </div>
      </div>

      <div class="gauge">
        <div class="row"><span>낙상 확률 p_fall</span><b id="pfall">0.00</b></div>
        <div class="track"><div class="fill" id="pfill" style="width:0%"></div></div>
        <div class="ticks">
          <i style="left:40%"></i><span style="left:40%">0.40 의심</span>
          <i style="left:60%"></i><span style="left:60%">0.60 낙상</span>
        </div>
      </div>

      <div class="meta">
        <div class="box"><div class="k">P(normal) 원출력</div><div class="v" id="pnormal">–</div></div>
        <div class="box"><div class="k">발행 코드</div><div class="v" id="code">NOR</div></div>
        <div class="box"><div class="k">추론 주기</div><div class="v">16 f</div></div>
        <div class="box"><div class="k">상태</div><div class="v" id="status" style="font-size:13px">idle</div></div>
      </div>
    </div>
  </section>

  <!-- 그래프들 -->
  <section style="display:grid;gap:18px">
    <div class="card">
      <div class="hd">순간 스펙트럼 · x = subcarrier, y = amplitude</div>
      <div class="bd"><canvas id="spectrum" height="150"></canvas></div>
    </div>
    <div class="card">
      <div class="hd">워터폴 · 위→아래 시간 흐름, 색 = amp</div>
      <div class="bd"><canvas id="waterfall" width="256" height="240"></canvas></div>
    </div>
  </section>
</main>

<script>
let mode="serial", interpret="iq", lastIdx=-1, maxAmp=1;

const spec=new Chart(document.getElementById("spectrum"),{
  type:"line",
  data:{labels:[],datasets:[{data:[],borderColor:"#3b82f6",borderWidth:2,pointRadius:0,
        fill:true,backgroundColor:"rgba(59,130,246,.12)",tension:.25}]},
  options:{animation:false,responsive:true,plugins:{legend:{display:false}},
    scales:{x:{ticks:{color:"#8a99a8",maxTicksLimit:16},grid:{color:"#161f29"}},
            y:{ticks:{color:"#8a99a8"},grid:{color:"#161f29"},beginAtZero:true}}}
});

const wf=document.getElementById("waterfall"), wctx=wf.getContext("2d");
function color(v){const t=Math.max(0,Math.min(1,v));
  const r=Math.round(255*Math.min(1,Math.max(0,1.5-Math.abs(4*t-3))));
  const g=Math.round(255*Math.min(1,Math.max(0,1.5-Math.abs(4*t-2))));
  const b=Math.round(255*Math.min(1,Math.max(0,1.5-Math.abs(4*t-1))));return[r,g,b];}
function pushWaterfall(amp){
  const w=amp.length||1; if(wf.width!==w) wf.width=w;
  const img=wctx.getImageData(0,0,wf.width,wf.height-1); wctx.putImageData(img,0,1);
  const row=wctx.createImageData(wf.width,1);
  for(let i=0;i<amp.length;i++){const[r,g,b]=color(amp[i]/maxAmp);
    row.data[i*4]=r;row.data[i*4+1]=g;row.data[i*4+2]=b;row.data[i*4+3]=255;}
  wctx.putImageData(row,0,0);
}

function setMode(m){mode=m;
  document.getElementById("btnSerial").classList.toggle("active",m==="serial");
  document.getElementById("btnFile").classList.toggle("active",m==="file");}
function setInterpret(v){interpret=v;
  document.getElementById("btnIq").classList.toggle("active",v==="iq");
  document.getElementById("btnAmp").classList.toggle("active",v==="amp");}
document.getElementById("btnSerial").onclick=()=>{setMode("serial");apply();};
document.getElementById("btnFile").onclick=()=>{setMode("file");apply();};
document.getElementById("btnIq").onclick=()=>setInterpret("iq");
document.getElementById("btnAmp").onclick=()=>setInterpret("amp");
document.getElementById("apply").onclick=apply;

function apply(){
  fetch("/api/apply",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({mode,cfg:{
      port:port.value,baud:baud.value,file:file.value,fps:fps.value,interpret}})});
}

const hero=document.getElementById("hero");
function render(d){
  document.getElementById("frames").textContent=d.frames;
  document.getElementById("winfill").textContent=d.win_count+"/"+d.seq_len;
  document.getElementById("nsub").textContent=d.n_sub||0;
  document.getElementById("status").textContent=d.status+(d.error?" — "+d.error:"");
  document.getElementById("livedot").className="dot"+(d.status==="running"?" live":"");

  // 서브캐리어 정합 경고
  const note=document.getElementById("note");
  if(d.n_sub && d.n_sub<64){ note.style.display="block";
    note.textContent=`⚠ 들어온 서브캐리어 ${d.n_sub}개 < 모델 입력 64. 부족분은 0으로 패딩됨 → 학습 분포와 어긋날 수 있음(정렬 확인 필요).`; }
  else note.style.display="none";

  // 상태 히어로
  const lab=d.label||"NORMAL";
  hero.className="card hero"+(lab==="FALL"?" fall":lab==="WARNING"?" warn":"");
  document.getElementById("glyph").textContent=lab==="FALL"?"🚨":lab==="WARNING"?"⚠️":"🧍";
  document.getElementById("statelabel").textContent=d.ready?lab:"버퍼 채우는 중";
  document.getElementById("statesub").textContent=
    !d.ready ? `윈도 ${d.win_count}/${d.seq_len} 프레임 수집 중…` :
    lab==="FALL"?"낙상 감지 — 즉시 확인 필요":
    lab==="WARNING"?"의심 동작 — 관찰 중":"정상 — 움직임 안정";
  document.getElementById("pfall").textContent=(d.p_fall??0).toFixed(2);
  document.getElementById("pnormal").textContent=d.ready?(d.p_normal??0).toFixed(3):"–";
  document.getElementById("code").textContent=d.ready?d.code:"—";
  document.getElementById("pfill").style.width=Math.round((d.p_fall||0)*100)+"%";

  if(d.idx===lastIdx||!d.amp||!d.amp.length) return;
  lastIdx=d.idx;
  const m=Math.max(...d.amp,1); maxAmp=maxAmp*0.95+m*0.05;
  spec.data.labels=d.amp.map((_,i)=>i);
  spec.data.datasets[0].data=d.amp; spec.update("none");
  pushWaterfall(d.amp);
}
setInterval(()=>fetch("/api/data").then(r=>r.json()).then(render).catch(()=>{}),80);
</script>
</body>
</html>"""

# ---------------------------------------------------------------------------
# Flask
# ---------------------------------------------------------------------------
app = Flask(__name__)


@app.route("/")
def index():
    return PAGE


@app.route("/api/data")
def api_data():
    return jsonify(monitor.snapshot())


@app.route("/api/apply", methods=["POST"])
def api_apply():
    body = request.get_json(silent=True) or {}
    monitor.apply(mode=body.get("mode"), cfg=body.get("cfg"))
    return jsonify(monitor.snapshot())


if __name__ == "__main__":
    print(f"weights: {WEIGHTS_H}")
    print("CSI 낙상 모니터: http://127.0.0.1:5000  (Ctrl+C 종료)")
    app.run(host="127.0.0.1", port=5000, threaded=True)
