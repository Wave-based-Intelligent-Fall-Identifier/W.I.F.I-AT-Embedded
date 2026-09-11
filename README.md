# W.I.F.I-AT-Embedded

WiFi CSI(Channel State Information) 기반 비접촉 낙상 감지 시스템 **WIFY**를 구성하는 ESP32 보드 중,
**사람 감지(PIR) + CSI 유발/캡처 + 온디바이스 GRU 낙상 추론 + MQTT 발행**을 담당하는 펌웨어 레포다.

> ⚠️ **레포명 주의**: 레포 이름은 "AT"이지만, 실제 역할은 **WiFi STA(클라이언트) + 송신·추론 보드**다.
> 짝을 이루는 `W.I.F.I-STA-Embedded` 레포가 실제로는 AP 역할을 하는 상대 보드다. 이 문서의 "AT 보드"는
> 항상 *이 레포가 올라가는 보드*를 가리킨다.

## 목차
- [시스템 내 위치](#시스템-내-위치)
- [주요 기능](#주요-기능)
- [하드웨어 / 기술 스택](#하드웨어--기술-스택)
- [레포 구조](#레포-구조)
- [빌드 및 플래시](#빌드-및-플래시)
- [MQTT 토픽](#mqtt-토픽)
- [온디바이스 AI (GRU 낙상 추론)](#온디바이스-ai-gru-낙상-추론)
- [관련 레포](#관련-레포)
- [확인 필요 / 알려진 이슈](#확인-필요--알려진-이슈)

## 시스템 내 위치

```
                         ┌──────────────────────────────────────────┐
                         │   이 레포: W.I.F.I-AT-Embedded (ESP32)   │
                         │   (실질 역할 = WiFi STA / 송신 / 추론)   │
                         │                                          │
   PIR 센서 ── GPIO4 ──▶ │  PIR 태스크 → 화장실 점유(restroom) 이벤트 │
                         │                                          │
   가정용 공유기(AP) ◀── │  WiFi STA 연결 + 주기 UDP 트래픽(50Hz)    │
        (2.4GHz)         │        └▶ 자신의 WiFi CSI 캡처(콜백)      │
                         │              └▶ baseline 차감/모션 에너지  │
                         │              └▶ GRU(71×12) 낙상 추론      │
                         │                                          │
   상대 ESP32 보드 ◀───▶ │  ESP-NOW: PIR 진입/퇴장 신호(command 1/2) │
   (W.I.F.I-STA-Embedded)│                                          │
                         └──────────────┬───────────────────────────┘
                                        │ MQTT publish/subscribe
                                        ▼
                         ┌──────────────────────────────┐
                         │  mosquitto 브로커 (W.I.F.I-Server) │
                         └──────────────┬───────────────┘
                                        ▼
                         ┌──────────────────────────────┐
                         │  앱 (W.I.F.I-APP-Client)      │
                         │  브로커를 직접 구독            │
                         └──────────────────────────────┘
```

- CSI 원본 수집과 GRU 추론은 **이 보드 자신**이 수행한다(`components/espnow`의 `csi_rx_cb`,
  `components/AI`의 `esp_ai_task`). 가정용 공유기로 향하는 더미 UDP 트래픽을 스스로 만들어 CSI를
  촘촘하게 유발시키는 구조다(`components/espnow/espnowAP.c` 참고).
- 상대 ESP32 보드(`W.I.F.I-STA-Embedded`)와는 **ESP-NOW**로만 연결되어 있으며, PIR이 사람을
  감지/미감지했을 때 `command=1`(진입)/`command=2`(퇴장) 신호만 주고받는다. 두 보드 CSI 측정이
  서로 어떤 식으로 결합되는지의 전체 그림은 상대 레포까지 봐야 확정된다(아래 "확인 필요" 참고).
- 낙상 판정 결과·기기 상태·화장실 점유 상태는 모두 **MQTT**로 브로커에 발행되고, 앱은 브로커를
  직접 구독한다.

## 주요 기능

- **PIR 기반 사람 감지 및 화장실 점유 이벤트**(`components/PIR`)
  사람 감지 시 `restroom=ACT` 발행, 10분간 CSI 감지 모드 유지 후 `DEACT` 발행. 배포 모드에서는
  대기 중 Deep Sleep(EXT1 GPIO 웨이크업)으로 전환해 절전한다.
- **WiFi CSI 자가 캡처 + 배경(baseline) 캘리브레이션**(`components/espnow`, `components/Baseline`)
  ESP32 WiFi CSI(LLTF/HT-LTF) 콜백으로 원시 진폭을 수집하고, 빈 공간 상태에서 서브캐리어별
  배경값(baseline)을 계산해 NVS에 저장·재사용한다. 이후 실시간 값에서 배경을 차감해 움직임
  에너지를 산출한다.
- **온디바이스 GRU 낙상 추론**(`components/AI`)
  71프레임 × 12서브캐리어(sc16~sc27) 슬라이딩 윈도우를 GRU 신경망에 통과시켜 낙상 확률을 계산하고,
  임계값에 따라 `NOR`(정상)/`WARN`(의심)/`DAN`(낙상)으로 판정한다. 상태가 바뀔 때만(에지) MQTT로
  발행해 트래픽을 줄인다.
- **MQTT 기반 상태 보고 및 원격 명령 처리**(`components/AppMQTT`)
  기기 online/offline(Last Will, retained), 하트비트, baseline 진행 상태, 현재 네트워크 정보를
  발행하고, baseline 재보정·WiFi 자격증명 원격 변경 명령을 구독·처리한다.
- **ESP-NOW 보드 간 시그널링**(`components/espnow`)
  PIR 진입/퇴장을 상대 ESP32 보드(`AP_MAC_ADDRESS`)에 알린다.
- **상태 LED 표시**(`components/gpio`)
  공유기 접속 여부를 내장 LED 점멸 패턴(느린 하트비트=접속됨 / 빠른 점멸=재접속 중)으로 표시한다.

## 하드웨어 / 기술 스택

| 구분 | 내용 |
|---|---|
| MCU / 타깃 | ESP32 (Xtensa), `idf.py set-target esp32` (`sdkconfig`에 `CONFIG_IDF_TARGET_ESP32=y` 확인) |
| 프레임워크 | ESP-IDF (버전 고정 파일 없음 — 로컬 `idf.py --version`으로 확인 필요. `.devcontainer`는 `espressif/idf` 공식 Docker 이미지 사용) |
| 센서/입출력 | PIR 모션 센서(GPIO4, EXT1 딥슬립 웨이크업), 상태 LED(GPIO2) |
| 무선 | WiFi STA(2.4GHz, WPA2-PSK), ESP-NOW(보드 간 시그널링), WiFi CSI(`CONFIG_ESP_WIFI_CSI_ENABLED=y`) |
| 통신 프로토콜 | MQTT (`esp-mqtt` 클라이언트, `mqtt5_init()` 함수명과 달리 코드상 `protocol_ver = MQTT_PROTOCOL_V_3_1_1`로 접속 — 확인 필요) |
| 온보드 AI | GRU(Gated Recurrent Unit) + Dense 2단, sigmoid 출력. 가중치(float32)를 헤더로 컴파일 임베드(`gru_weights.h`, 실측 약 17.6KB) |
| 저장소 | NVS(baseline 캘리브레이션 값 영속화) |
| 빌드 시스템 | CMake (ESP-IDF `idf_component_register`) |

## 레포 구조

git 레포 루트에는 이 README와 `.omc/`, `.git/` 등만 있고, **실제 ESP-IDF 프로젝트는 한 단계 아래
중첩 폴더**에 있다.

```
W.I.F.I-AT-Embedded/                       # git 레포 루트 (이 파일 위치)
└── W.I.F.I-AT-Embedded/                   # 실제 ESP-IDF 프로젝트 루트
    ├── CMakeLists.txt                     # project(W.I.F.I-AT-Embedded)
    ├── sdkconfig.defaults                 # CSI 활성화, MQTT v5 컴파일 옵션 기본값
    ├── main/
    │   ├── main.c                         # app_main: 초기화 순서 + 태스크 생성
    │   ├── CMakeLists.txt
    │   └── Kconfig.projbuild              # "Secret Configuration" 메뉴 (SSID/브로커/기기ID 등)
    ├── components/
    │   ├── AI/            # espAI.c(추론 태스크) + gru_functions.c/.h(GRU 순전파) + gru_weights.h(가중치)
    │   ├── AppMQTT/        # MQTT 초기화·발행·구독 처리(AppMQTT.c/Send.c/Recv.c)
    │   ├── APcommon/       # 공용 설정 헤더(APconfig.h): WiFi/브로커 매크로, 상대 보드 MAC, TEST 모드 플래그
    │   ├── Baseline/       # CSI 배경(baseline) 캘리브레이션·차감·NVS 저장
    │   ├── espnow/         # WiFi STA 연결, ESP-NOW 페어링, CSI 콜백, CSI 유발 UDP 트래픽 태스크
    │   ├── gpio/           # PIR/LED GPIO 초기화, 상태 LED 태스크
    │   └── PIR/            # PIR 태스크(딥슬립/웨이크업, 화장실 점유 이벤트)
    └── tools/
        └── ai_verify/      # 보드 없이 GRU 이식을 검증하는 스크립트 모음(자체 README 보유)
```

각 컴포넌트의 `CMakeLists.txt` 기준 의존 관계(`REQUIRES`/`PRIV_REQUIRES`) 요약:

| 컴포넌트 | 의존 |
|---|---|
| `AI` | `freertos`, `Baseline`, `espnow`, `gpio`, `driver`, `esp_ringbuf` |
| `AppMQTT` | `mqtt`, `esp_timer`, `nvs_flash`, `esp_netif`, `esp_event`, `esp_wifi`, `APcommon`, `Baseline` |
| `APcommon` | `Baseline`, `AppMQTT` (헤더만 제공; `.gitignore`상 `include/`와 `CMakeLists.txt`만 추적됨) |
| `Baseline` | `mqtt`, `AppMQTT`, `nvs_flash` |
| `espnow` | `freertos`, `esp_wifi`, `log`, `nvs_flash`, `esp_event`, `PIR`, `APcommon`, `Baseline`, `lwip`, `esp_netif` |
| `gpio` | `driver` |
| `PIR` | `driver`, `esp_wifi`, `freertos`, `gpio`, `APcommon`, `AppMQTT` |
| `main` | `PIR`, `espnow`, `gpio`, `AppMQTT`, `mqtt`, `AI`, `Baseline` |

## 빌드 및 플래시

실제 프로젝트 루트로 이동한 뒤 진행한다(레포 루트 기준 한 단계 더 들어감).

```powershell
cd W.I.F.I-AT-Embedded\W.I.F.I-AT-Embedded

# 1) ESP-IDF 환경 활성화 (설치 경로에 맞게, 공식 export 스크립트/배치 사용)
#    예: . $env:IDF_PATH\export.ps1

# 2) 타깃 설정
idf.py set-target esp32

# 3) 비밀 설정 입력 (menuconfig > "Secret Configuration")
idf.py menuconfig
```

`menuconfig` → **Secret Configuration** 메뉴에서 아래 값을 반드시 로컬에서 채운다
(`sdkconfig`는 `.gitignore`에 포함되어 커밋되지 않으므로, 보드/개발자마다 매번 설정 필요):

| Kconfig 항목 | 설명 | 기본값 |
|---|---|---|
| `ESP_WIFI_SSID` / `ESP_WIFI_PASSWORD` | 연결할 공유기(2.4GHz, WPA2) 자격증명 | `myssid` / `mypassword` |
| `ESP_BROKER_URI` | MQTT 브로커 주소 | `mqtt://127.0.0.1:1883` |
| `ESP_MQTT_USERNAME` / `ESP_MQTT_PASSWORD` | MQTT 로그인 자격증명 | `wify` / `changeme` |
| `WIFY_DEVICE_ID` | MQTT 토픽 세그먼트(`wify/<이 값>/...`), 다기기 운용 시 기기별로 변경 | `device01` |
| `ESP_MAC_ADDR` | (현재 미사용) 상대 ESP32(AP) MAC — 실제로는 `components/APcommon/include/APconfig.h`의 `AP_MAC_ADDRESS` 매크로를 직접 수정해야 반영됨 | `0xFF,...,0xFF` |

`sdkconfig.defaults`에 `CONFIG_ESP_WIFI_CSI_ENABLED=y`, `CONFIG_MQTT_PROTOCOL_5=y`가 이미
지정되어 있어 별도 수정은 불필요하다.

```powershell
# 4) 빌드
idf.py build

# 5) 플래시 + 모니터 (COM 포트는 장치관리자에서 확인)
idf.py -p COM<n> flash monitor
```

VS Code + Dev Container로 작업하는 경우 `.devcontainer/`(espressif/idf 공식 이미지 기반)를 그대로
사용할 수 있다.

배포 전 확인 사항: `components/APcommon/include/APconfig.h`의 `#define TEST`는 딥슬립을
비활성화하고 항시 동작시키는 디버그 플래그다. 코드 주석대로 **실제 배포 시에는 주석 처리**해야
PIR 미감지 시 정상적으로 Deep Sleep에 들어간다.

## MQTT 토픽

토픽 prefix는 `wify/<WIFY_DEVICE_ID>`(기본 `wify/device01`)이며, `components/AppMQTT/include/originFunc.h`의
`WIFY_TOPIC()` 매크로로 컴파일타임에 빌드된다.

### 발행 (이 보드 → 브로커)

| 토픽 | 페이로드 | QoS / Retain | 설명 |
|---|---|---|---|
| `.../status` | `online` / `offline` | 1 / retained | 접속 시 `online`, LWT로 `offline` — 둘 다 retained라 늦게 접속한 앱도 즉시 현재 상태 확인 가능 |
| `.../heartbeat` | `{"alive":true,"uptime":<sec>}` | 0 / - | 30초 주기 생존 신호 |
| `.../AI` | `DAN` \| `WARN` \| `NOR` | 1 / - | GRU 낙상 판정 결과(상태 전이 시에만 발행) |
| `.../restroom` | `LOAD` \| `ACT` \| `DEACT` | 1 / - | 부팅 시 `LOAD`, PIR 감지 시 `ACT`, 10분 무감지 후 `DEACT` |
| `.../baseline/status` | `MEASURING` \| `DONE` | 1 / - | CSI 배경 캘리브레이션 진행 상태 |
| `.../nownetwork` | `{"networkid":"<ssid>"}` | 1 / - | 현재 연결된 WiFi SSID |
| `.../nownetwork/status` | `connect` \| `disconnect` | 1 / - | WiFi 연결 상태(평문) |
| `.../nownetwork/again` | `AGAIN` | 1 / - | id/passwd 형식 오류 시 재입력 요청(평문) |

### 구독 (브로커 → 이 보드, 명령)

| 토픽 | 페이로드 | 동작 |
|---|---|---|
| `.../baseline/cmd` | `{"cmd":"BASELINE_REBUILD"}` (구 평문 `BASELINEREBUILD`도 허용) | 다음 CSI 프레임부터 baseline 재캘리브레이션 |
| `.../edit/nownetwork` | `id:<ssid> passwd:<password>` (평문) | 새 WiFi 자격증명을 임시 저장(즉시 적용 아님) |
| `.../edit/editnetwork` | `NEWNETWORKEDIT` | 위에서 저장한 자격증명으로 WiFi 재연결 실제 적용 |

## 온디바이스 AI (GRU 낙상 추론)

`components/AI`가 담당하며, 가중치는 별도 `W.I.F.I-AI` 레포에서 학습해 `gru_weights.h`로
export/임베드된다.

- 입력: 71프레임(시퀀스) × 12서브캐리어(`sc16~sc27`) raw amplitude, baseline 미차감
- 추론 주기: 새 CSI 프레임 `FALL_INFER_STRIDE`(기본 16)개마다 1회
- 출력: sigmoid 확률 → `GRU_OUTPUT_IS_P_NORMAL` 매크로에 따라 낙상확률(p_fall)로 변환
- 판정: `p_fall >= 0.50` → `DAN`, `0.40 <= p_fall < 0.50` → `WARN`, 그 외 → `NOR` (상태 전이 시에만 발행)

보드 없이 가능한 검증(타깃 컴파일 성립, 알고리즘/가중치 수치 sanity, 실데이터 근사 평가)은
`tools/ai_verify/README.md`에 정리되어 있다. 해당 문서 기준 실데이터 근사 평가에서 AUC 0.977,
최적임계 정확도 95.8%가 보고되었으나, 학습 텐서와 평가 CSV의 형상이 완전히 일치하지 않는 근사치이며
**실기기 기준 일반화 정확도는 별도 검증이 필요**하다고 명시되어 있다.

## 관련 레포

| 레포 | 역할 |
|---|---|
| `W.I.F.I-STA-Embedded` | 짝을 이루는 ESP32 보드 펌웨어(레포명과 반대로 실질 AP 역할) |
| `W.I.F.I-Server` | MQTT 브로커(mosquitto) 등 서버 인프라 |
| `W.I.F.I-APP-Client` | 브로커를 직접 구독하는 클라이언트 앱 |
| `W.I.F.I-AI` | GRU 낙상 모델 학습, `gru_weights.h` export 담당 |

(각 레포의 정확한 저장소 URL은 이 레포 안에서 확인되지 않아 표기하지 않음 — 확인 필요)

## 확인 필요 / 알려진 이슈

- **ESP-IDF 정확한 버전**: 레포 내 버전 고정 파일이 없다. 로컬 `idf.py --version`으로 확인 필요.
- **MQTT 프로토콜 버전**: 함수명은 `mqtt5_init()`이나 `AppMQTT.c`의 실제 설정은
  `.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1`이다. `sdkconfig.defaults`에는
  `CONFIG_MQTT_PROTOCOL_5=y`가 있어 의도와 실제 코드가 다른 것으로 보인다 — 담당자 확인 필요.
- **두 보드 간 CSI 역할 분담**: 이 보드는 자신의 WiFi CSI를 스스로 캡처해 추론하는 구조이고,
  ESP-NOW는 PIR 진입/퇴장 신호 전달용으로만 쓰인다. `APconfig.h` 주석에는 상대 보드의 AP(`wify_csi_ap`)에
  직접 접속하는 대안 모드(`bssid_set=true`) 언급도 있어, 두 보드가 CSI 측면에서 정확히 어떻게
  상호작용하는지는 `W.I.F.I-STA-Embedded` 레포까지 함께 봐야 확정된다.
- **GRU 관련 미확정 파라미터**(`tools/ai_verify/README.md`에 명시): 입력 정규화 방식, 서브캐리어
  정합(데이터셋 64채널 vs 펌웨어 baseline 52채널), 판정 임계값의 실기기 최종 튜닝 여부.
- **관련 레포 URL**: 이 레포 내에서 확인되지 않음.
