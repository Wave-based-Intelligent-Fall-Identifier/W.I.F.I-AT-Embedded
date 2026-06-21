#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
graph_flask.py — CSI amp 실시간 그래퍼 (UART / CSV 재생 스왑)

펌웨어는 건드리지 않는다. 이 도구는 두 가지 소스를 버튼으로 스왑하며 그린다.
  [Serial] UART로 흘러나오는 값을 그대로 읽어 파싱 → amp 그래프
  [File]   저장해 둔 CSV를 한 줄씩 재생 → amp 그래프 (라이브 기기 불필요)

라인 파싱 규칙 (두 소스 공통):
  - ESP-IDF 로그 접두사 'I (12345) TAG: ' 는 떼어낸다.
  - 대괄호 [...] 가 있으면 그 안의 숫자만 쓴다 (표준 CSI 덤프 형식).
  - 없으면 라인 전체에서 숫자를 추출한다 (CSV 등).
  - interpret='iq'  : 숫자를 [허수,실수, 허수,실수, ...] 로 보고 amp=sqrt(re^2+im^2)
                      (펌웨어 espAI.c 의 amp 계산과 동일한 의미)
  - interpret='amp' : 숫자를 이미 계산된 amp 로 보고 그대로 그린다.

의존성:
  pip install flask pyserial
실행:
  python graph_flask.py
  브라우저에서 http://127.0.0.1:5000
"""

import math
import os
import re
import threading
from flask import Flask, jsonify, request

# ---------------------------------------------------------------------------
# 라인 -> amp 파서
# ----------------------------------------------------------------------------
LOG_PREFIX_RE = re.compile(r"^[IWEDV]\s*\(\d+\)\s*[\w\-/.]+:\s*")
NUM_RE = re.compile(r"-?\d+\.?\d*(?:[eE][-+]?\d+)?")


def line_to_amp(line, interpret="iq", min_nums=16):
    """UART/CSV 한 줄을 amp 리스트로. 데이터가 아니면 None."""
    line = LOG_PREFIX_RE.sub("", line.strip())
    if not line:
        return None

    if "[" in line and "]" in line:
        line = line[line.index("[") + 1 : line.rindex("]")]

    nums = [float(x) for x in NUM_RE.findall(line)]
    if len(nums) < min_nums:
        return None  # 로그 잡음/짧은 줄 무시

    if interpret == "amp":
        return nums

    # iq: 인접 2개씩 (허수, 실수) 쌍 -> 크기
    amp = []
    for i in range(len(nums) // 2):
        im = nums[2 * i]
        re_ = nums[2 * i + 1]
        amp.append(math.sqrt(re_ * re_ + im * im))
    return amp


# ----------------------------------------------------------------------------
# 데이터 소스 리더 (스왑 가능)
# ----------------------------------------------------------------------------
class Reader:
    def __init__(self):
        self.lock = threading.Lock()
        self.stop_evt = threading.Event()
        self.thread = None

        self.mode = "file"  # 'serial' | 'file'
        self.cfg = {
            "port": "COM3",
            "baud": 115200,
            "file": "csi_capture.csv",
            "fps": 20,
            "interpret": "iq",   # 'iq' | 'amp'
            "min_nums": 16,
        }

        self.latest = []
        self.idx = 0           # 프레임이 갱신될 때마다 증가 (프런트가 새 프레임 감지용)
        self.frames = 0        # 누적 프레임 수
        self.status = "idle"   # idle | running | stopped | error
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

    def _push(self, amp):
        with self.lock:
            self.latest = amp
            self.idx += 1
            self.frames += 1

    def snapshot(self):
        with self.lock:
            return {
                "idx": self.idx,
                "amp": self.latest,
                "mode": self.mode,
                "status": self.status,
                "error": self.error,
                "frames": self.frames,
                "n": len(self.latest),
                "cfg": dict(self.cfg),
            }

    # --- 시리얼 모드 ---
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
                amp = line_to_amp(
                    raw.decode("utf-8", "replace"),
                    self.cfg["interpret"],
                    self.cfg["min_nums"],
                )
                if amp:
                    self._push(amp)
        except Exception as e:  # noqa: BLE001
            self.status, self.error = "error", f"시리얼 읽기 오류: {e}"
        finally:
            try:
                ser.close()
            except Exception:  # noqa: BLE001
                pass
            if self.status == "running":
                self.status = "stopped"

    # --- 파일 재생 모드 ---
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
                        amp = line_to_amp(
                            line, self.cfg["interpret"], self.cfg["min_nums"]
                        )
                        if amp:
                            self._push(amp)
                            if self.stop_evt.wait(delay):
                                break
                if self.stop_evt.wait(0.5):  # 끝까지 재생 후 잠깐 쉬고 반복
                    break
        except Exception as e:  # noqa: BLE001
            self.status, self.error = "error", f"파일 재생 오류: {e}"
        finally:
            if self.status == "running":
                self.status = "stopped"


reader = Reader()

# ----------------------------------------------------------------------------
# 웹 페이지 (한 파일 유지를 위해 인라인)
# ----------------------------------------------------------------------------
PAGE = r"""<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>CSI amp 그래퍼</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js@4"></script>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font-family: system-ui, sans-serif; background:#0d1117; color:#e6edf3; }
  header { padding:12px 16px; border-bottom:1px solid #30363d; display:flex; gap:12px; align-items:center; flex-wrap:wrap; }
  h1 { font-size:16px; margin:0 12px 0 0; }
  button { background:#21262d; color:#e6edf3; border:1px solid #30363d; border-radius:6px; padding:6px 12px; cursor:pointer; }
  button.active { background:#1f6feb; border-color:#1f6feb; }
  input { background:#0d1117; color:#e6edf3; border:1px solid #30363d; border-radius:6px; padding:5px 8px; width:90px; }
  input.wide { width:200px; }
  label { font-size:12px; opacity:.8; margin-left:8px; }
  .status { margin-left:auto; font-size:13px; }
  .ok { color:#3fb950; } .err { color:#f85149; } .idle { color:#8b949e; }
  main { padding:16px; display:grid; gap:16px; grid-template-columns:1fr; }
  .card { background:#161b22; border:1px solid #30363d; border-radius:8px; padding:12px; }
  .card h2 { font-size:13px; margin:0 0 8px; opacity:.8; font-weight:600; }
  #waterfall { width:100%; height:280px; image-rendering:pixelated; background:#000; border-radius:4px; }
</style>
</head>
<body>
<header>
  <h1>CSI amp 그래퍼</h1>
  <button id="btnSerial">Serial (UART)</button>
  <button id="btnFile">File (CSV)</button>

  <label>port</label><input id="port" value="COM3">
  <label>baud</label><input id="baud" value="115200">
  <label>file</label><input id="file" class="wide" value="csi_capture.csv">
  <label>fps</label><input id="fps" value="20">
  <label>해석</label>
  <button id="btnIq" class="active">I/Q→amp</button>
  <button id="btnAmp">amp 그대로</button>
  <button id="apply">적용/연결</button>

  <span class="status idle" id="status">idle</span>
</header>

<main>
  <div class="card">
    <h2>순간 스펙트럼 (x = subcarrier, y = amp)</h2>
    <canvas id="spectrum" height="120"></canvas>
  </div>
  <div class="card">
    <h2>워터폴 (위→아래 = 시간 흐름, 색 = amp, 가로 = subcarrier)</h2>
    <canvas id="waterfall" width="256" height="280"></canvas>
  </div>
</main>

<script>
let mode = "file";
let interpret = "iq";
let lastIdx = -1;
let maxAmp = 1;

// 스펙트럼 라인 차트
const spec = new Chart(document.getElementById("spectrum"), {
  type: "line",
  data: { labels: [], datasets: [{ data: [], borderColor:"#1f6feb", borderWidth:1.5,
           pointRadius:0, fill:true, backgroundColor:"rgba(31,111,235,.15)", tension:.2 }] },
  options: { animation:false, responsive:true, plugins:{legend:{display:false}},
             scales:{ x:{ticks:{color:"#8b949e",maxTicksLimit:14}, grid:{color:"#21262d"}},
                      y:{ticks:{color:"#8b949e"}, grid:{color:"#21262d"}, beginAtZero:true} } }
});

// 워터폴
const wf = document.getElementById("waterfall");
const wctx = wf.getContext("2d");
function color(v){ // 0..1 -> 파랑->청록->노랑->빨강
  const t = Math.max(0, Math.min(1, v));
  const r = Math.round(255*Math.min(1, Math.max(0, 1.5-Math.abs(4*t-3))));
  const g = Math.round(255*Math.min(1, Math.max(0, 1.5-Math.abs(4*t-2))));
  const b = Math.round(255*Math.min(1, Math.max(0, 1.5-Math.abs(4*t-1))));
  return [r,g,b];
}
function pushWaterfall(amp){
  const w = amp.length || 1;
  if (wf.width !== w) wf.width = w;
  // 한 줄 아래로 스크롤
  const img = wctx.getImageData(0, 0, wf.width, wf.height-1);
  wctx.putImageData(img, 0, 1);
  // 맨 윗줄에 새 프레임
  const row = wctx.createImageData(wf.width, 1);
  for (let i=0;i<amp.length;i++){
    const [r,g,b] = color(amp[i]/maxAmp);
    row.data[i*4]=r; row.data[i*4+1]=g; row.data[i*4+2]=b; row.data[i*4+3]=255;
  }
  wctx.putImageData(row, 0, 0);
}

function setMode(m){
  mode = m;
  document.getElementById("btnSerial").classList.toggle("active", m==="serial");
  document.getElementById("btnFile").classList.toggle("active", m==="file");
}
function setInterpret(v){
  interpret = v;
  document.getElementById("btnIq").classList.toggle("active", v==="iq");
  document.getElementById("btnAmp").classList.toggle("active", v==="amp");
}

document.getElementById("btnSerial").onclick = ()=>{ setMode("serial"); apply(); };
document.getElementById("btnFile").onclick   = ()=>{ setMode("file");   apply(); };
document.getElementById("btnIq").onclick  = ()=> setInterpret("iq");
document.getElementById("btnAmp").onclick = ()=> setInterpret("amp");
document.getElementById("apply").onclick = apply;

function apply(){
  fetch("/api/apply", { method:"POST", headers:{"Content-Type":"application/json"},
    body: JSON.stringify({
      mode,
      cfg: {
        port: document.getElementById("port").value,
        baud: document.getElementById("baud").value,
        file: document.getElementById("file").value,
        fps:  document.getElementById("fps").value,
        interpret
      }
    })
  });
}

function render(d){
  const s = document.getElementById("status");
  s.className = "status " + (d.status==="running"?"ok":d.status==="error"?"err":"idle");
  s.textContent = `[${d.mode}] ${d.status}` + (d.error? " — "+d.error : "")
                  + ` · frames=${d.frames} · n=${d.n}`;
  if (d.idx === lastIdx || !d.amp || !d.amp.length) return;
  lastIdx = d.idx;

  const m = Math.max(...d.amp, 1);
  maxAmp = maxAmp*0.95 + m*0.05;  // 부드러운 자동 스케일

  spec.data.labels = d.amp.map((_,i)=>i);
  spec.data.datasets[0].data = d.amp;
  spec.update("none");
  pushWaterfall(d.amp);
}

setInterval(()=> fetch("/api/data").then(r=>r.json()).then(render).catch(()=>{}), 80);
</script>
</body>
</html>"""

# ----------------------------------------------------------------------------
# Flask
# ----------------------------------------------------------------------------
app = Flask(__name__)


@app.route("/")
def index():
    return PAGE


@app.route("/api/data")
def api_data():
    return jsonify(reader.snapshot())


@app.route("/api/apply", methods=["POST"])
def api_apply():
    body = request.get_json(silent=True) or {}
    reader.apply(mode=body.get("mode"), cfg=body.get("cfg"))
    return jsonify(reader.snapshot())


if __name__ == "__main__":
    print("CSI amp 그래퍼: http://127.0.0.1:5000  (Ctrl+C 종료)")
    app.run(host="127.0.0.1", port=5000, threaded=True)
