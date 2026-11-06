# 🐹✨ GeekClock — 치이카와 × 긱매직(GeekMagic SmallTV) 커스텀 스튜디오

긱매직 미니 와이파이 데스크탑 디지털 날씨 시계(`SmallTV` / `SmallTV-Ultra` / `SmallTV-Pro`, 240×240 IPS LCD)에 **치이카와 · 하치와레 · 우사기 캐릭터 애니메이션 + 실시간 시간 + 실시간 기온(Open-Meteo) + 응원 문구 / D-Day**를 렌더링하여 Wi-Fi로 전송하는 로컬 웹 스튜디오 & 자동 동기화 도구입니다.

---

## ✨ 주요 기능

* **240×240 전체화면 움직이는 GIF & Baseline JPEG 렌더링 (`chiikawa_art.py`)**
  * 2배 슈퍼샘플링(480×480 → 240×240 LANCZOS) 기반의 선명한 캐릭터/한글 타이포그래피 렌더링
  * **내장 캐릭터**: 치이카와, 하치와레, 우사기, 삼총사 함께 보기 (활짝 웃음 / 사스마타 응원 / 윙크 / 울먹 / 낮잠 표정)
  * **커스텀 GIF·이미지 오버레이**: 내 컴퓨터의 GIF 움짤이나 PNG/JPG 사진 위에 실시간 시간·온도·D-Day 합성 지원
* **실시간 날씨/기온 자동 연동 (`geekmagic_client.py`)**
  * 별도 API 키 없이 `Open-Meteo`를 통해 서울, 판교, 수원, 인천, 부산 등 주요 도시의 현재 기온과 최고/최저 기온 자동 표시
* **기기 자동 탐색 & 펌웨어 호환**
  * 로컬 네트워크 내 긱매직 시계 IP 자동 스캔 (`/space.json`, `/app.json`, `/city.json`)
  * `SmallTV-Ultra / Pro` (240×240 전체화면 `/doUpload?dir=/image/`, `/set?img=...`) 및 구형 1세대 `SmallTV` (80×80 `/upload`, `/set?gif=10`) 모두 지원
* **로컬 웹 스튜디오 & 백그라운드 자동 갱신 (`studio.py`)**
  * `http://127.0.0.1:8765` 에서 실시간 미리보기 및 원클릭 시계 전송

---

## 🚀 실행 방법

### 1. 더블클릭 실행 (macOS)
Finder에서 **`실행하기.command`** 파일을 더블클릭하면 브라우저에서 웹 스튜디오(`http://127.0.0.1:8765`)가 열립니다.

### 2. 터미널 실행
```bash
python3 studio.py
```

백그라운드 데몬 모드로만 실행하려면:
```bash
python3 studio.py --daemon --ip 172.16.1.104
```
