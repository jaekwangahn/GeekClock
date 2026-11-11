# Chiikawa Standalone Firmware for GeekMagic SmallTV-Ultra (ESP8266)

맥북이나 로컬 서버가 완전히 꺼져 있어도 **시계 단독으로** 동작하는 커스텀 ESP8266 펌웨어입니다.

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
~/.platformio/penv/bin/pio run
```
