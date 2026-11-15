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
