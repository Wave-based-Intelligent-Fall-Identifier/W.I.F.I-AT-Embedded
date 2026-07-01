# AI(GRU 낙상) 이식 — 보드 없는 검증

브랜치 `AI-combine`. AI 담당자가 준 `gru_weights.h`(가중치) + `gru_functions.c`(추론)를
AT 펌웨어(`components/AI`)에 연동하고, **보드 없이** 가능한 검증까지 담았다.

## 무엇이 되어 있나
- `components/AI/gru_weights.h` — GRU 가중치(float32). 컴파일 후 플래시 **약 17.6KB**(실측).
- `components/AI/gru_functions.c` — GRU(reset_after) 순전파 + Dense2단. 71x64 → sigmoid.
- `components/AI/include/gru_functions.h` — 프로토타입/형상 매크로(신규).
- `components/AI/espAI.c` — 추론 경로 연동: raw 71x64 윈도 → `gru_predict` → `wify/device01/AI`
  로 `DAN/WARN/NOR` 발행(에지 검출). 기존 5특징/스텁 경로 제거.
- `components/AI/CMakeLists.txt` — `gru_functions.c` 빌드 등록.

## 검증 방법 (3층)
| # | 스크립트 | 검증 대상 | 여기서 실행됨 |
|---|----------|-----------|:---:|
| 1 | `build_check.sh` (A) | ESP32(xtensa) **타깃 컴파일** — 문법/타입/배열크기 | ✅ PASS (obj 17,628 B) |
| 2 | `gru_reference.py` | **알고리즘/가중치** — 차원·범위·결정성·민감도 | ✅ PASS |
| 3 | `build_check.sh` (B) / `test_gru_host.c` | 친구 C코드 **직접 실행** (네이티브 gcc 필요) | ⏭ SKIP (native gcc 없음) |

```bash
bash tools/ai_verify/build_check.sh      # (1) 타깃 컴파일 + (3) 네이티브 실행(있으면)
python tools/ai_verify/gru_reference.py  # (2) 수치 검증
```

## 검증의 한계 (정직하게)
- 위 검증은 **컴파일 성립 + 수치 sanity**까지다. **실제 낙상 정확도는 검증하지 않는다.**
- 전체 펌웨어 링크(`espAI.c` 포함)는 IDF 의존성이 많아 `idf.py build` 필요(이 환경엔 idf.py 없음):
  ```
  cd W.I.F.I-AT-Embedded && idf.py build && idf.py size
  ```
- 실기기 동작은 송신(sender) 보드 전원 복구 후 필요.

## ⛳ 남은 확정 항목 (AI 담당자 확인 → espAI.c 상단 매크로만 수정)
1. **출력 극성** `GRU_OUTPUT_IS_P_NORMAL` — 학습 라벨에서 class 1 이 정상인가 낙상인가?
   (현재 1=정상 가정. 반대면 0 으로.)
2. **정규화** `GRU_INPUT_NORMALIZE` — 학습 때 입력 표준화했나? 했다면 mean/std 를 받아 `apply_input_norm` 구현.
3. **임계값** `GRU_PFALL_DANGER/WARNING` — 검증 데이터로 튜닝.
4. **서브캐리어 정합** — 실제 CSI 콜백이 64개 서브캐리어를 주는지 확인
   (dataset=64, 펌웨어 baseline=52. 모델 경로는 raw 64폭으로 별도 처리, 부족분 zero-pad).
