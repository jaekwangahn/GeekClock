# Chiikawa Standalone Firmware for GeekMagic SmallTV Ultra / Pro

맥북이나 로컬 서버가 완전히 꺼져 있어도 **시계 단독으로** 동작하는 커스텀 펌웨어입니다. SmallTV-Ultra(ESP8266)와 SmallTV-Pro(ESP32)는 각각의 빌드 대상을 사용합니다.

## 주요 기능
- **완전 독립형 NTP 시간 동기화**: 공유기 Wi-Fi만 있으면 `time.google.com` (`time.apple.com`, `kr.pool.ntp.org` 백업 및 UDP 차단 환경용 HTTP `Date` 헤더 폴백 내장)에서 한국 표준시(`KST, UTC+9`)를 실시간 동기화합니다.
- **실시간 날씨 연동**: 15분마다 `api.open-meteo.com`에서 현재 기온, 최저/최고 기온, 날씨 아이콘(맑음·구름·흐림·비·눈)을 직접 가져옵니다.
- **무깜빡임(Zero-Flicker) 치이카와 애니메이션**: `240x240` ST7789 IPS 화면(`SPI_MODE3`, 40MHz)에 `치이카와 / 하치와레 / 우사기 / 삼총사` 4프레임 애니메이션과 5가지 파스텔 테마를 부드럽게 렌더링합니다.
- **내장 한글 웹 설정 페이지 (`http://<시계IP>/`)**:
  - 밝기 슬라이더 (`GPIO5` PWM 즉시 제어 + 심야 자동 저밝기)
  - 캐릭터 / 테마 / 12·24시간제 / 초(`:SS`) 표시 / 도시 / NTP 서버 변경
  - 모든 한글 음절(11,172자) 및 기호를 지원하는 하단 응원 문구 & D-Day 카운터
- **100% 무선 복구 안전장치**:
  - 공유기 연결 실패 시 자동으로 **`Chiikawa-Clock`** Wi-Fi AP(`http://192.168.4.1/`)가 열려 스마트폰이나 노트북에서 새 Wi-Fi를 설정할 수 있습니다.
  - **`http://<시계IP>/update`** 에서 언제든 새 `.bin` 또는 `stock_backup/FW-Smalltv-Ultra-V9.0.31.bin`(순정 펌웨어)을 업로드해 원클릭 복구할 수 있습니다.

## 빌드 방법 (PlatformIO)
```bash
# 1. 스프라이트 & 한글 안티앨리어싱 폰트 헤더 생성 (선택)
python3 tools/generate_assets.py

# 2. 펌웨어 빌드 (.pio/build/geekmagic_ultra/firmware.bin)
~/.platformio/penv/bin/pio run -e geekmagic_ultra
```

## SmallTV Pro (ESP32, 8 MB) 전용 빌드

firmware 디렉터리에서 PlatformIO로 빌드합니다.

```bash
pio run -e geekmagic_pro
```

결과: `.pio/build/geekmagic_pro/firmware.bin` (OTA 앱 전용).
대상: GeekMagic SmallTV-PRO, ESP32. ESP8266 Ultra / ESP32-C2에 설치하지 마세요.
화면: ST7789 240×240, SCLK=18, MOSI=23, DC=2, RST=4, CS=GND,
백라이트=25(active-low), SPI mode 3 / 40 MHz.
핀 근거: https://github.com/giovi321/smalltv-mod/blob/main/src/board_esp32_pro.h

순정 `/update`의 `firmware` 필드로 앱 파일만 전송합니다.
기존 부트로더와 파티션 표는 유지하며, 파일시스템을 자동 포맷하지 않습니다.
빌드 파티션 설정은 장치의 OTA 슬롯 크기를 변경하지 않습니다.
기존 Wi-Fi NVS 자격 증명을 사용하고, 접속 실패 시 `Chiikawa-Clock` AP를 엽니다.
설치 후 `/v.json`, `/api/status`, `/update`로 기종·상태·복구 경로를 확인합니다.
V1.2부터 상단 터치 버튼(GPIO32/T9)을 누를 때마다 치이카와 → 하치와레 → 우사기 → 삼총사 → 치이카와 순서로 전환합니다.
길게 대고 있어도 한 번만 바뀌며, 손을 뗀 뒤 다시 눌러야 다음 캐릭터로 넘어갑니다.
마지막 캐릭터 선택은 저장되어 재부팅 후에도 유지됩니다.
부팅 직후 약 1초는 터치 기준값을 자동 보정하므로 손을 떼어 두세요.
`/api/status`에 터치 보정·측정값·누름 횟수 진단 정보가 포함됩니다.
순정 복구에는 Pro용 `FW_SmallTV-PRO_V3.4.88EN.bin`을 사용합니다.

배포 파일: [SmallTV-PRO_Chiikawa_ESP32_V1.2.bin](SmallTV-PRO_Chiikawa_ESP32_V1.2.bin)

무결성 확인: [크기 및 SHA256 manifest](SmallTV-PRO_Chiikawa_ESP32_V1.2.json).
V1.2는 Pro 기기에 OTA 설치 후 재부팅, Wi-Fi, NTP, 날씨, 웹 설정, 터치 센서 보정을 확인했습니다.
터치 입력 로직은 호스트 시뮬레이션으로 보정·잡음·디바운스·길게 누르기·재누르기·타이머 순환을 검증했습니다.

## Pro V1.3 치이카와 다마고치

상단을 빠르게 두 번 터치하면 시계와 다마고치 화면을 오갑니다.
더블 터치를 판별하기 위해 한 번 터치 동작은 손을 뗀 후 약 0.35초 뒤 반영됩니다.

| 조작 | 시계 | 다마고치 |
| --- | --- | --- |
| 한 번 터치 | 캐릭터 순환 | 놀아주기 |
| 두 번 터치 | 다마고치로 이동 | 시계로 돌아가기 |
| 약 0.9초 길게 터치 | 동작 없음 | 밥 주기 |

치이카와가 정원을 돌아다니며 배부름·행복·기력이 조금씩 줄어듭니다.
밥과 놀이로 경험치와 레벨이 오르고, 기력이 부족하면 자동으로 낮잠을 잡니다.
웹 설정에서도 밥·놀이·휴식과 모드 변경을 할 수 있습니다.
게임은 별도 서버 없이 기기 단독으로 동작하며, 게임 상태와 모드는 NVS에 저장합니다.
전원이 꺼져 있던 동안의 상태 감소는 계산하지 않습니다.

게임 중에는 주기적인 날씨 HTTP 요청을 잠시 미뤄 터치와 이동이 멈추지 않게 합니다.
시계로 돌아가면 필요한 날씨 갱신이 재개됩니다.

```bash
# GeekClock 루트에서 실행
clang++ -std=c++17 -Wall -Wextra -Werror firmware/tests/pet_logic_test.cpp -o /tmp/pet-logic-test
/tmp/pet-logic-test
.venv/bin/python firmware/tools/preview_pet.py --output /tmp/geekmagic-pet-preview
.venv/bin/pio run -d firmware -e geekmagic_pro
```

게임 표정·한글 폰트 재생성: `.venv/bin/python firmware/tools/generate_pet_assets.py`.
`/api/status`의 `pet` 및 `/api/pet`에서 게임 상태를 조회할 수 있습니다.
`POST /api/pet`은 예를 들어 `{"action":"feed"}`를 받으며, action 값은 `enter`, `exit`, `feed`, `play`, `sleep` 중 하나입니다.

## Pro V1.4 하드웨어 사양 표시

V1.4 OTA 파일: [SmallTV-PRO_Chiikawa_ESP32_V1.4.bin](SmallTV-PRO_Chiikawa_ESP32_V1.4.bin) · [크기 및 SHA256](SmallTV-PRO_Chiikawa_ESP32_V1.4.json)

V1.4에는 위의 다마고치 게임도 포함됩니다.

웹 설정의 `하드웨어 사양`을 펼치면 칩 모델·리비전, CPU 코어와 현재 클럭,
실제 플래시 용량·동작 클럭·모드, 내부 힙·PSRAM 감지 결과, 앱과 OTA 슬롯 크기를 봅니다.
`/api/status`의 `hardware`에 원시 바이트 단위와 보드 설정을 함께 제공합니다.
화면·GPIO·화면 SPI 설정 및 제조사 문서 사양은 런타임 측정값과 구분합니다.
힙 용량은 칩 전체 SRAM 용량이 아니며, Bluetooth 하드웨어 지원과 펌웨어 활성 상태도 별개입니다.
플래시 동작 클럭은 기존 부트로더 헤더의 설정 대신 현재 클럭 레지스터에서 읽습니다.

## SmallTV Pro 최신 펌웨어 V1.7

[OTA 펌웨어](SmallTV-PRO_Chiikawa_ESP32_V1.7.bin) · [크기 및 SHA256](SmallTV-PRO_Chiikawa_ESP32_V1.7.json)

시계·다마고치·엔화 환율·마리오 타이틀 시계를 기기 단독으로 실행합니다.
상단 더블 터치는 **시계 → 다마고치 → 엔화 환율 → 마리오 시계 → 시계** 순서입니다.
웹 설정에서도 화면 선택과 각 모드의 동작을 실행할 수 있습니다.

| 화면 | 한 번 터치 | 약 0.9초 길게 터치 |
| --- | --- | --- |
| 시계 | 캐릭터 변경 | 동작 없음 |
| 다마고치 | 놀아주기 | 밥 주기 |
| 엔화 환율 | 다시 조회 | 기본 시계로 복귀 |
| 마리오 시계 | 점프 | 기본 시계로 복귀 |

선택한 화면과 다마고치 상태는 재부팅 후에도 유지됩니다.

## 일본 엔화 환율

100엔당 원화(KRW)를 표시하며, ECB 기준일과 기기 조회 시각(KST)을 구분합니다.
데이터: [Frankfurter ECB 기준환율](https://api.frankfurter.dev/v2/providers/ecb/rate/jpy/krw).
공시 참고환율이며 은행의 현찰 매수·매도 가격이나 실시간 체결 가격이 아닙니다.
공시가 없는 주말·휴일에는 최근 영업일 기준일이 표시됩니다.

인증서 검증 HTTPS 요청을 별도 작업에서 실행해 터치와 게임이 멈추지 않게 합니다.
15분마다 확인하고, 실패하면 60초 뒤 재시도합니다. 수동 조회는 30초 간격입니다.
마지막 정상 환율은 NVS에 저장하고, 조회 실패·오프라인·재부팅 중에는 저장값임을 표시합니다.

조회 API: `GET /api/exchange`, `POST /api/exchange/refresh`.
화면 선택: `POST /api/screen`에 예를 들어 `{"mode":"exchange"}`를 전송합니다.
모드 값: `clock`, `pet`, `exchange`, `mario`.

## 마리오 타이틀 시계

[참고 이미지](assets/mario_title_reference.png)의 VS. SUPER MARIO BROS. 타이틀 화면을
240×240에 맞췄습니다. 파란 배경·주황 타이틀 보드·상단 HUD·초록 언덕·벽돌 바닥 위에
현재 한국 시각과 날짜를 표시하고, 작은 마리오가 돌아다니며 점프합니다.
기존 초 표시·12/24시간제와 화면 밝기·심야 밝기 설정을 따릅니다.

[타이틀 시계 애니메이션](../previews/preview_mario_title.gif)

점프 API: `POST /api/mario/jump`. 상태는 `/api/status`의 `mario`에서 확인합니다.

## Pro 검증 및 자산 재생성

GeekClock 루트에서 실행합니다. `clang++`, Pillow, PlatformIO가 필요합니다.

```bash
clang++ -std=c++17 -Wall -Wextra -Werror firmware/tests/exchange_rate_test.cpp -o /tmp/exchange-rate-test
/tmp/exchange-rate-test
clang++ -std=c++17 -Wall -Wextra -Werror firmware/tests/mario_scene_test.cpp -o /tmp/mario-scene-test
/tmp/mario-scene-test
.venv/bin/python firmware/tools/preview_exchange.py --output /tmp/geekmagic-exchange-preview
.venv/bin/python firmware/tools/preview_mario.py --output /tmp/geekmagic-mario-preview
.venv/bin/pio run -d firmware -e geekmagic_pro
```

환율 글꼴: `.venv/bin/python firmware/tools/generate_exchange_assets.py`.
마리오 스프라이트: `.venv/bin/python firmware/tools/generate_mario_assets.py`.
타이틀 배경·글꼴: `.venv/bin/python firmware/tools/generate_mario_title_assets.py`.

V1.7 빌드·이미지 형식·해시 검증과 기기 OTA 설치를 완료했습니다.
실제 기기에서 네 화면 전환, 한국 시각·날짜·초 표시, 점프 시작/종료, 환율 수신과
기존 설정·돌봄 기록 보존을 확인했습니다. 네이티브 테스트와 실제 렌더러 미리보기에는
ASan/UBSan 및 경계·시간 형식 검증을 사용했습니다.
