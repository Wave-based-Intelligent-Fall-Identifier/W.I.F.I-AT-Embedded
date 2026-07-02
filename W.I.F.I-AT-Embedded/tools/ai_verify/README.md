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

## 실데이터 평가 결과 (eval_on_dataset.py, csi_dataset_clean.csv 72샘플, 근사)
- 출력평균 normal=0.876 / fall=0.161, **분리도 AUC=0.977**, 최적임계 정확도 95.8%.
- **극성 확정**: 출력↑ = normal → `GRU_OUTPUT_IS_P_NORMAL=1` (현재 코드와 일치, 뒤집을 필요 없음).
- 정규화: raw 입력으로도 잘 분리됨 → raw 학습일 가능성 높음(`GRU_INPUT_NORMALIZE=0` 유지 타당).
- 임계값: 경험적 최적 p_fall≈0.59 ≈ 현재 `GRU_PFALL_DANGER=0.60`.
- ⚠️ 근사(학습 텐서 88×71×64 ≠ CSV 72×≤70, 마지막프레임 반복 패딩) + 학습데이터 포함 가능 →
  95.8%는 낙관적. '판별력 있음 + 극성 방향 맞음'은 견고, 일반화 정확도는 실기기 필요.

## ⛳ 남은 확정 항목
1. **출력 극성** — ✅ 실데이터로 =1 확정.
2. **정규화** — 🟡 raw 유력(위 근거). AI 담당자에게 최종 확인 권장.
3. **임계값** `GRU_PFALL_DANGER/WARNING` — 🟡 현재값 적정. 실기기서 미세 튜닝.
4. **서브캐리어 정합** — 🔴 실기기서 CSI 콜백이 64채널 주는지 확인 필요
   (dataset=64, 펌웨어 baseline=52. 모델 경로는 raw 64폭 별도 처리, 부족분 zero-pad).

## 발견 견고화 (discovery hardening)
- `AppMQTT.c`: 기기 status "online" 을 **retained** 로 발행(`mqtt_publish_retained`).
  기존엔 online=비retained인데 last_will(offline)=retained라, 늦게 접속한 앱이 기기를
  "offline" 으로 오인. 이제 online/offline 둘 다 retained → 브로커에 현재 상태 항상 보관.
- 이벤트 토픽(AI/restroom 등)은 retained 금지(늦은 구독자에 옛 알림 오발 방지).
