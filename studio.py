#!/usr/bin/env python3
"""
GeekMagic Chiikawa Custom Screen Studio & Auto-Sync Server.
Run `python3 studio.py` (or double-click `실행하기.command`) to open the Web Studio at http://127.0.0.1:8765
"""

from __future__ import annotations
import argparse
import base64
import json
import os
import sys
import threading
import time
import urllib.parse
import webbrowser
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, Optional

from chiikawa_art import render_clock_gif, render_clock_jpeg, render_smalltv_80x80_gif
from geekmagic_client import (
    KOREAN_CITIES,
    control_device,
    discover_geekmagic_devices,
    fetch_weather,
    get_device_brightness,
    probe_geekmagic_ip,
    push_to_geekmagic,
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.environ.get("GEEKCLOCK_DATA_DIR") or BASE_DIR
try:
    os.makedirs(DATA_DIR, exist_ok=True)
    test_file = os.path.join(DATA_DIR, ".write_test")
    with open(test_file, "w") as _tf:
        _tf.write("ok")
    os.remove(test_file)
except Exception:
    DATA_DIR = os.path.expanduser("~/Library/Application Support/GeekClock")
    os.makedirs(DATA_DIR, exist_ok=True)

CONFIG_PATH = os.path.join(DATA_DIR, "config.json")
CUSTOM_MEDIA_DIR = os.path.join(DATA_DIR, "custom_media")
os.makedirs(CUSTOM_MEDIA_DIR, exist_ok=True)

DEFAULT_CONFIG: Dict[str, Any] = {
    "device_ip": "",
    "character": "chiikawa",
    "mood": "happy",
    "theme": "pink",
    "layout": "character_focus",
    "city_key": "seoul",
    "city_label": "",
    "use_manual_weather": False,
    "manual_temp": 18,
    "manual_code": "sunny",
    "use_12h": False,
    "custom_text": "오늘도 어떻게든 되겠지!",
    "dday_title": "",
    "dday_date": "",
    "auto_sync": True,
    "as_gif": True,
    "brightness": 80,
    "custom_media_filename": "",
}

STATE_LOCK = threading.Lock()
PUSH_LOCK = threading.Lock()
LAST_SYNC_STATUS: Dict[str, Any] = {
    "last_push_time": None,
    "last_result": None,
}


def load_config() -> Dict[str, Any]:
    cfg = dict(DEFAULT_CONFIG)
    if os.path.exists(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8") as f:
                saved = json.load(f)
            if isinstance(saved, dict):
                cfg.update(saved)
        except Exception:
            pass
    return cfg


def save_config(cfg: Dict[str, Any]) -> None:
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)


def get_custom_media_bytes(cfg: Dict[str, Any]) -> Optional[bytes]:
    if cfg.get("character") != "custom":
        return None
    fn = cfg.get("custom_media_filename") or ""
    if not fn:
        return None
    path = os.path.join(CUSTOM_MEDIA_DIR, os.path.basename(fn))
    if os.path.exists(path):
        try:
            with open(path, "rb") as f:
                return f.read()
        except Exception:
            return None
    return None


def resolve_weather_for_config(cfg: Dict[str, Any]) -> Dict[str, Any]:
    if cfg.get("use_manual_weather"):
        return fetch_weather(
            city_key=cfg.get("city_key", "seoul"),
            manual_temp=int(cfg.get("manual_temp", 18)),
            manual_code=cfg.get("manual_code", "sunny"),
        )
    return fetch_weather(city_key=cfg.get("city_key", "seoul"))


def build_and_push_now(cfg: Dict[str, Any], as_gif: bool = False) -> Dict[str, Any]:
    with PUSH_LOCK:
        weather = resolve_weather_for_config(cfg)
        custom_bytes = get_custom_media_bytes(cfg)
        now = datetime.now()
        smalltv_gif = render_smalltv_80x80_gif(cfg, custom_image_bytes=custom_bytes)
        theme_key = cfg.get("theme", "pink")
        if as_gif:
            payload = render_clock_gif(cfg, weather, now=now, custom_image_bytes=custom_bytes)
            res = push_to_geekmagic(
                cfg.get("device_ip", ""),
                payload,
                is_gif=True,
                filename="chiikawa.gif",
                smalltv_gif_bytes=smalltv_gif,
                theme_key=theme_key,
            )
        else:
            payload = render_clock_jpeg(cfg, weather, now=now, custom_image_bytes=custom_bytes)
            res = push_to_geekmagic(
                cfg.get("device_ip", ""),
                payload,
                is_gif=False,
                filename="chiikawa.jpg",
                smalltv_gif_bytes=smalltv_gif,
                theme_key=theme_key,
            )

        with STATE_LOCK:
            LAST_SYNC_STATUS["last_push_time"] = now.strftime("%H:%M:%S")
            LAST_SYNC_STATUS["last_result"] = res
        return res


def auto_sync_worker() -> None:
    """Background thread that pushes updated time & temperature when auto_sync is enabled."""
    last_pushed_slot = ""
    while True:
        try:
            cfg = load_config()
            if cfg.get("auto_sync") and cfg.get("device_ip", "").strip():
                as_gif = bool(cfg.get("as_gif", True))
                now = datetime.now()
                # For GIF mode, sync every 3 minutes so animation loops smoothly without frequent flash writes
                slot_min = (now.minute // 3) * 3 if as_gif else now.minute
                now_slot = f"{now.strftime('%Y-%m-%d %H')}:{slot_min:02d}:{as_gif}"
                if now_slot != last_pushed_slot:
                    build_and_push_now(cfg, as_gif=as_gif)
                    last_pushed_slot = now_slot
            else:
                last_pushed_slot = ""
        except Exception:
            pass
        time.sleep(3.0)


STUDIO_HTML = r"""<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>치이카와 x 긱매직 시계 커스텀 스튜디오</title>
<style>
  :root {
    --pink: #ff6b9d;
    --pink-soft: #ffeef4;
    --pink-border: #ffc2d6;
    --bg: #fff8fa;
    --card: #ffffff;
    --text: #3d2c35;
    --sub: #8c6f7d;
    --blue: #4a9fe6;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Apple SD Gothic Neo", "Pretendard", sans-serif;
    background: linear-gradient(160deg, #fff3f7 0%, #f3f9ff 100%);
    color: var(--text);
    min-height: 100vh;
  }
  header {
    background: rgba(255, 255, 255, 0.88);
    backdrop-filter: blur(12px);
    border-bottom: 2px solid var(--pink-border);
    padding: 16px 28px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    position: sticky;
    top: 0;
    z-index: 20;
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .brand h1 {
    font-size: 20px;
    margin: 0;
    font-weight: 800;
    color: #3a2630;
  }
  .badge-pill {
    background: var(--pink-soft);
    color: var(--pink);
    border: 1px solid var(--pink-border);
    padding: 4px 10px;
    border-radius: 999px;
    font-size: 12px;
    font-weight: 700;
  }
  .container {
    max-width: 1120px;
    margin: 26px auto;
    padding: 0 20px 60px;
    display: grid;
    grid-template-columns: 380px 1fr;
    gap: 26px;
    align-items: start;
  }
  @media (max-width: 880px) {
    .container { grid-template-columns: 1fr; }
  }
  .panel {
    background: var(--card);
    border-radius: 24px;
    border: 2px solid #f4dce5;
    box-shadow: 0 12px 32px rgba(255, 107, 157, 0.08);
    padding: 24px;
  }
  .preview-sticky {
    position: sticky;
    top: 86px;
    text-align: center;
  }
  /* Realistic GeekMagic Mini TV Bezel */
  .clock-bezel {
    width: 288px;
    height: 306px;
    margin: 8px auto 18px;
    background: linear-gradient(145deg, #fffdf9, #f3ece2);
    border-radius: 38px;
    padding: 22px 24px 36px;
    box-shadow:
      0 18px 38px rgba(61, 44, 53, 0.16),
      inset 0 2px 4px rgba(255, 255, 255, 0.9),
      inset 0 -6px 12px rgba(0, 0, 0, 0.06);
    border: 3px solid #e6ddd0;
    position: relative;
  }
  .screen-glass {
    width: 240px;
    height: 240px;
    background: #1a1a24;
    border-radius: 20px;
    overflow: hidden;
    box-shadow: inset 0 0 0 3px #2b2b36;
    display: flex;
    align-items: center;
    justify-content: center;
  }
  .screen-glass img {
    width: 240px;
    height: 240px;
    display: block;
    image-rendering: auto;
  }
  .bezel-label {
    position: absolute;
    bottom: 10px;
    left: 0;
    right: 0;
    font-size: 11px;
    font-weight: 800;
    letter-spacing: 1.5px;
    color: #b4a89a;
  }
  .section-title {
    font-size: 15px;
    font-weight: 800;
    margin: 0 0 12px;
    display: flex;
    align-items: center;
    gap: 8px;
    color: #3d2c35;
  }
  .section-box {
    padding-bottom: 20px;
    margin-bottom: 20px;
    border-bottom: 1px dashed #eed7e0;
  }
  .section-box:last-child {
    border-bottom: none;
    margin-bottom: 0;
    padding-bottom: 0;
  }
  .chip-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(125px, 1fr));
    gap: 8px;
  }
  .chip-btn {
    border: 2px solid #f0dde4;
    background: #fff;
    color: var(--text);
    border-radius: 14px;
    padding: 10px 12px;
    font-size: 13px;
    font-weight: 700;
    cursor: pointer;
    transition: all 0.15s ease;
    text-align: center;
  }
  .chip-btn:hover {
    border-color: var(--pink-border);
    background: #fff9fb;
  }
  .chip-btn.active {
    border-color: var(--pink);
    background: var(--pink-soft);
    color: #d63874;
    box-shadow: 0 4px 12px rgba(255, 107, 157, 0.16);
  }
  .row {
    display: flex;
    gap: 10px;
    align-items: center;
    margin-bottom: 10px;
  }
  input[type="text"], input[type="number"], input[type="date"], select {
    width: 100%;
    padding: 10px 13px;
    border-radius: 12px;
    border: 2px solid #ead5de;
    font-size: 14px;
    font-family: inherit;
    outline: none;
    transition: border-color 0.15s;
  }
  input:focus, select:focus {
    border-color: var(--pink);
  }
  .btn {
    border: none;
    border-radius: 14px;
    padding: 12px 16px;
    font-size: 14px;
    font-weight: 800;
    cursor: pointer;
    transition: transform 0.1s, box-shadow 0.15s;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 6px;
  }
  .btn:active { transform: scale(0.98); }
  .btn-primary {
    background: linear-gradient(135deg, #ff6b9d, #ff4f87);
    color: white;
    width: 100%;
    box-shadow: 0 8px 20px rgba(255, 82, 138, 0.3);
  }
  .btn-secondary {
    background: #eef6ff;
    color: #2b7ac9;
    border: 2px solid #cde3ff;
    width: 100%;
  }
  .btn-outline {
    background: #fff;
    color: var(--text);
    border: 2px solid #e6d0d9;
  }
  .preset-quote {
    display: inline-block;
    background: #fdf2f6;
    color: #7d5265;
    border: 1px solid #f3d2e0;
    border-radius: 999px;
    padding: 4px 10px;
    font-size: 12px;
    margin: 4px 4px 0 0;
    cursor: pointer;
    font-weight: 600;
  }
  .preset-quote:hover {
    background: var(--pink-soft);
    color: var(--pink);
  }
  .status-banner {
    margin-top: 12px;
    padding: 11px 14px;
    border-radius: 12px;
    font-size: 13px;
    font-weight: 600;
    display: none;
    text-align: left;
    line-height: 1.45;
  }
  .status-ok {
    display: block;
    background: #eafaf1;
    color: #1f7a4c;
    border: 1px solid #b7ebc9;
  }
  .status-err {
    display: block;
    background: #fff0f0;
    color: #c93535;
    border: 1px solid #f7c4c4;
  }
  .status-info {
    display: block;
    background: #eef6ff;
    color: #2563eb;
    border: 1px solid #bfdbfe;
  }
  .toggle-box {
    display: flex;
    align-items: center;
    justify-content: space-between;
    background: #fff7fa;
    border: 2px solid #f7d6e3;
    padding: 12px 14px;
    border-radius: 14px;
    margin-top: 10px;
  }
  .help-tip {
    font-size: 12px;
    color: var(--sub);
    line-height: 1.5;
    margin-top: 6px;
  }
</style>
</head>
<body>

<header>
  <div class="brand">
    <span style="font-size:26px;">🐹✨</span>
    <div>
      <h1>치이카와 x 긱매직 시계 스튜디오</h1>
    </div>
    <span class="badge-pill">240x240 IPS 전용</span>
  </div>
  <div style="display:flex; align-items:center; gap:8px;">
    <div id="syncBadge" class="badge-pill" style="background:#f3f9ff;color:#3b82f6;border-color:#bfdbfe;">
      실시간 미리보기 준비 완료
    </div>
    <button class="chip-btn" style="padding:5px 10px; font-size:12px; color:#c93535; border-color:#f5c2c7;" onclick="shutdownServer()">
      🛑 서버 종료
    </button>
  </div>
</header>

<div class="container">
  <!-- Left Column: Live 240x240 Clock Preview & Push Actions -->
  <div class="panel preview-sticky">
    <div class="section-title" style="justify-content:center;">📺 실제 시계 화면 미리보기 (240×240)</div>
    <div class="clock-bezel">
      <div class="screen-glass">
        <img id="previewImg" src="/api/preview.gif" alt="Clock Preview">
      </div>
      <div class="bezel-label">GEEKMAGIC SMALLTV</div>
    </div>

    <div style="display:flex; flex-direction:column; gap:9px;">
      <button class="btn btn-primary" onclick="pushToClock(true)">
        🎞️ 움직이는 애니메이션 GIF로 시계에 전송 (추천)
      </button>
      <button class="btn btn-secondary" onclick="pushToClock(false)">
        🚀 정지 화면(선명한 JPG)으로 시계에 전송
      </button>
      <div style="display:flex; gap:8px;">
        <a id="dlJpg" href="/api/preview.jpg?download=1" download="chiikawa_clock.jpg" class="btn btn-outline" style="flex:1; text-decoration:none; font-size:12px; padding:9px;">
          💾 JPG 파일 저장
        </a>
        <a id="dlGif" href="/api/preview.gif?download=1" download="chiikawa_clock.gif" class="btn btn-outline" style="flex:1; text-decoration:none; font-size:12px; padding:9px;">
          💾 GIF 움짤 저장
        </a>
      </div>
    </div>

    <div class="toggle-box">
      <div style="text-align:left;">
        <div style="font-size:13px; font-weight:800;">🔄 1분마다 시간·온도 자동 갱신</div>
        <div style="font-size:11px; color:var(--sub);">켜두면 매분 시계 화면의 시간/기온을 자동 업데이트합니다</div>
      </div>
      <input type="checkbox" id="autoSyncToggle" style="width:20px; height:20px; accent-color:var(--pink); cursor:pointer;" onchange="onControlChange()">
    </div>

    <div id="statusBox" class="status-banner"></div>
  </div>

  <!-- Right Column: Customization Controls -->
  <div class="panel">

    <!-- 1. Device IP & Brightness -->
    <div class="section-box">
      <div class="section-title">📡 1. 긱매직 시계 연결 & 화면 밝기 조절</div>
      <div class="row">
        <input type="text" id="deviceIp" placeholder="예: 172.16.1.104 또는 192.168.0.25" onchange="onControlChange()">
        <button class="btn btn-secondary" style="width:auto; white-space:nowrap;" onclick="scanDevices()">
          🔍 기기 IP 자동 찾기
        </button>
      </div>

      <!-- Brightness Slider & Presets -->
      <div style="background:#fff7fa; border:2px solid #f7d6e3; border-radius:16px; padding:14px 16px; margin-top:12px;">
        <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px;">
          <span style="font-size:13px; font-weight:800;">🔆 시계 화면 밝기</span>
          <span id="brtLabel" style="font-size:14px; font-weight:800; color:var(--pink);">80%</span>
        </div>
        <div style="display:flex; align-items:center; gap:10px;">
          <span style="font-size:12px; color:var(--sub);">어둡게</span>
          <input type="range" id="brtSlider" min="0" max="100" step="5" value="80"
                 style="flex:1; accent-color:var(--pink); cursor:pointer;"
                 oninput="document.getElementById('brtLabel').innerText = this.value + '%'"
                 onchange="setClockBrightness(this.value)">
          <span style="font-size:12px; color:var(--sub);">밝게</span>
        </div>
        <div style="display:flex; gap:6px; margin-top:10px; flex-wrap:wrap;">
          <button class="chip-btn" style="flex:1; padding:7px 8px; font-size:12px;" onclick="setClockBrightness(5)">🌙 최소(5%)</button>
          <button class="chip-btn" style="flex:1; padding:7px 8px; font-size:12px;" onclick="setClockBrightness(25)">🔉 은은(25%)</button>
          <button class="chip-btn" style="flex:1; padding:7px 8px; font-size:12px;" onclick="setClockBrightness(55)">🌤️ 보통(55%)</button>
          <button class="chip-btn" style="flex:1; padding:7px 8px; font-size:12px;" onclick="setClockBrightness(80)">☀️ 밝음(80%)</button>
          <button class="chip-btn" style="flex:1; padding:7px 8px; font-size:12px;" onclick="setClockBrightness(100)">🔆 최대(100%)</button>
        </div>
      </div>

      <div class="help-tip">
        💡 <b>팁:</b> 슬라이더를 움직이거나 밝기 버튼을 누르면 <b>시계 화면 밝기가 즉시 변경</b>됩니다.
      </div>
    </div>

    <!-- 2. Character Selection -->
    <div class="section-box">
      <div class="section-title">🎀 2. 치이카와 캐릭터 & 표정 선택</div>
      <div class="chip-grid" id="charGrid" style="margin-bottom:12px;">
        <button class="chip-btn" data-val="chiikawa" onclick="selectChip('character','chiikawa')">🐹 치이카와</button>
        <button class="chip-btn" data-val="hachiware" onclick="selectChip('character','hachiware')">🐱 하치와레</button>
        <button class="chip-btn" data-val="usagi" onclick="selectChip('character','usagi')">🐰 우사기</button>
        <button class="chip-btn" data-val="trio" onclick="selectChip('character','trio')">✨ 삼총사 함께</button>
        <button class="chip-btn" data-val="custom" onclick="selectChip('character','custom')">🖼️ 내 움짤/사진</button>
      </div>

      <!-- Custom Image/GIF Upload Box -->
      <div id="customUploadBox" style="display:none; background:#fff7fa; border:2px dashed var(--pink-border); border-radius:14px; padding:14px; margin-bottom:12px;">
        <div style="font-size:13px; font-weight:700; margin-bottom:8px;">📁 내 컴퓨터의 치이카와 GIF 움짤 또는 PNG/JPG 이미지 올리기</div>
        <input type="file" id="mediaFileInput" accept="image/gif,image/png,image/jpeg,image/webp" onchange="uploadCustomMedia()" style="margin-bottom:8px;">
        <div class="row" style="margin-bottom:0;">
          <label style="font-size:12px; font-weight:700;">배치 방식:</label>
          <select id="layoutMode" onchange="onControlChange()" style="width:auto;">
            <option value="character_focus">중앙 캐릭터 스티커형 (파스텔 카드 배경)</option>
            <option value="full_custom_bg">화면 꽉 채우기 (240x240 배경 + 반투명 시계바)</option>
          </select>
        </div>
      </div>

      <div style="font-size:13px; font-weight:700; margin-bottom:8px; color:var(--sub);">표정 / 포즈</div>
      <div class="chip-grid" id="moodGrid">
        <button class="chip-btn" data-val="happy" onclick="selectChip('mood','happy')">😊 활짝 웃음</button>
        <button class="chip-btn" data-val="cheer" onclick="selectChip('mood','cheer')">🪄 사스마타 응원</button>
        <button class="chip-btn" data-val="wink" onclick="selectChip('mood','wink')">😉 귀여운 윙크</button>
        <button class="chip-btn" data-val="cry" onclick="selectChip('mood','cry')">🥹 울먹울먹</button>
        <button class="chip-btn" data-val="sleep" onclick="selectChip('mood','sleep')">💤 새근새근</button>
      </div>
    </div>

    <!-- 3. Color Theme -->
    <div class="section-box">
      <div class="section-title">🎨 3. 파스텔 배경 테마</div>
      <div class="chip-grid" id="themeGrid">
        <button class="chip-btn" data-val="pink" onclick="selectChip('theme','pink')">🌸 피치 핑크</button>
        <button class="chip-btn" data-val="sky" onclick="selectChip('theme','sky')">💎 소다 블루</button>
        <button class="chip-btn" data-val="butter" onclick="selectChip('theme','butter')">🍮 커스터드 옐로우</button>
        <button class="chip-btn" data-val="lavender" onclick="selectChip('theme','lavender')">💜 라벤더 드림</button>
        <button class="chip-btn" data-val="night" onclick="selectChip('theme','night')">🌙 파자마 나이트</button>
      </div>
    </div>

    <!-- 4. Time & Temperature Settings -->
    <div class="section-box">
      <div class="section-title">🌡️ 4. 시간 & 날씨(온도) 설정</div>
      <div class="row">
        <div style="flex:1;">
          <label style="font-size:12px; font-weight:700; color:var(--sub);">지역 선택 (실시간 기온 자동 연동)</label>
          <select id="cityKey" onchange="onControlChange()">
            <option value="seoul">서울</option>
            <option value="pangyo">판교 / 성남</option>
            <option value="suwon">수원</option>
            <option value="incheon">인천</option>
            <option value="goyang">고양 / 일산</option>
            <option value="yongin">용인</option>
            <option value="busan">부산</option>
            <option value="daegu">대구</option>
            <option value="daejeon">대전</option>
            <option value="gwangju">광주</option>
            <option value="ulsan">울산</option>
            <option value="sejong">세종</option>
            <option value="cheongju">청주</option>
            <option value="jeonju">전주</option>
            <option value="chuncheon">춘천</option>
            <option value="jeju">제주</option>
          </select>
        </div>
        <div style="flex:1;">
          <label style="font-size:12px; font-weight:700; color:var(--sub);">시간 표시 형식</label>
          <select id="use12h" onchange="onControlChange()">
            <option value="false">24시간제 (14:30)</option>
            <option value="true">12시간제 (02:30 PM)</option>
          </select>
        </div>
      </div>

      <div style="margin-top:8px;">
        <label style="font-size:13px; font-weight:700; cursor:pointer; display:flex; align-items:center; gap:6px;">
          <input type="checkbox" id="useManualWeather" onchange="onControlChange()">
          날씨/온도를 수동으로 직접 지정하기
        </label>
      </div>

      <div id="manualWeatherRow" class="row" style="display:none; margin-top:8px;">
        <div style="flex:1;">
          <label style="font-size:12px; color:var(--sub);">표시할 온도 (°C)</label>
          <input type="number" id="manualTemp" value="18" min="-30" max="50" onchange="onControlChange()">
        </div>
        <div style="flex:1;">
          <label style="font-size:12px; color:var(--sub);">날씨 아이콘</label>
          <select id="manualCode" onchange="onControlChange()">
            <option value="sunny">☀️ 맑음</option>
            <option value="partly_cloudy">⛅ 구름 조금</option>
            <option value="cloudy">☁️ 흐림</option>
            <option value="rain">🌧️ 비</option>
            <option value="snow">❄️ 눈</option>
          </select>
        </div>
      </div>
    </div>

    <!-- 5. Custom Message & D-Day -->
    <div class="section-box">
      <div class="section-title">💬 5. 하단 한줄 문구 & 디데이(D-Day)</div>
      <div style="margin-bottom:10px;">
        <label style="font-size:12px; font-weight:700; color:var(--sub);">하단 말풍선 메시지 (비워두면 숨김)</label>
        <input type="text" id="customText" placeholder="오늘도 어떻게든 되겠지!" oninput="debouncedPreview()">
        <div style="margin-top:4px;">
          <span class="preset-quote" onclick="setQuote('오늘도 어떻게든 되겠지!')">오늘도 어떻게든 되겠지!</span>
          <span class="preset-quote" onclick="setQuote('야하~! 신나는 하루!')">야하~! 신나는 하루!</span>
          <span class="preset-quote" onclick="setQuote('토벌 성공! 맛있는 거 먹자~')">토벌 성공! 맛있는 거 먹자~</span>
          <span class="preset-quote" onclick="setQuote('우라~! 오늘도 화이팅!')">우라~! 오늘도 화이팅!</span>
          <span class="preset-quote" onclick="setQuote('')">[문구 숨기기]</span>
        </div>
      </div>

      <div class="row" style="margin-bottom:0;">
        <div style="flex:1;">
          <label style="font-size:12px; font-weight:700; color:var(--sub);">D-Day 이름 (선택)</label>
          <input type="text" id="ddayTitle" placeholder="예: 휴가 / 시험 / 생일" oninput="debouncedPreview()">
        </div>
        <div style="flex:1;">
          <label style="font-size:12px; font-weight:700; color:var(--sub);">D-Day 목표 날짜 (선택)</label>
          <input type="date" id="ddayDate" onchange="onControlChange()">
        </div>
      </div>
    </div>

  </div>
</div>

<script>
let currentState = {
  device_ip: "",
  character: "chiikawa",
  mood: "happy",
  theme: "pink",
  layout: "character_focus",
  city_key: "seoul",
  use_manual_weather: false,
  manual_temp: 18,
  manual_code: "sunny",
  use_12h: false,
  custom_text: "오늘도 어떻게든 되겠지!",
  dday_title: "",
  dday_date: "",
  auto_sync: false
};

let debounceTimer = null;

function setStatus(msg, type="info") {
  const box = document.getElementById("statusBox");
  box.className = "status-banner status-" + type;
  box.innerHTML = msg;
}

function updateChipUI() {
  document.querySelectorAll("#charGrid .chip-btn").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.val === currentState.character);
  });
  document.querySelectorAll("#moodGrid .chip-btn").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.val === currentState.mood);
  });
  document.querySelectorAll("#themeGrid .chip-btn").forEach(btn => {
    btn.classList.toggle("active", btn.dataset.val === currentState.theme);
  });
  document.getElementById("customUploadBox").style.display =
    currentState.character === "custom" ? "block" : "none";
  document.getElementById("manualWeatherRow").style.display =
    currentState.use_manual_weather ? "flex" : "none";
}

function selectChip(field, val) {
  currentState[field] = val;
  updateChipUI();
  saveAndRefreshPreview();
}

function setQuote(text) {
  document.getElementById("customText").value = text;
  onControlChange();
}

function readFormIntoState() {
  currentState.device_ip = document.getElementById("deviceIp").value.trim();
  currentState.layout = document.getElementById("layoutMode").value;
  currentState.city_key = document.getElementById("cityKey").value;
  currentState.use_12h = document.getElementById("use12h").value === "true";
  currentState.use_manual_weather = document.getElementById("useManualWeather").checked;
  currentState.manual_temp = parseInt(document.getElementById("manualTemp").value || "18", 10);
  currentState.manual_code = document.getElementById("manualCode").value;
  currentState.custom_text = document.getElementById("customText").value;
  currentState.dday_title = document.getElementById("ddayTitle").value;
  currentState.dday_date = document.getElementById("ddayDate").value;
  currentState.auto_sync = document.getElementById("autoSyncToggle").checked;
}

function onControlChange() {
  readFormIntoState();
  updateChipUI();
  saveAndRefreshPreview();
}

function debouncedPreview() {
  if (debounceTimer) clearTimeout(debounceTimer);
  debounceTimer = setTimeout(() => {
    onControlChange();
  }, 260);
}

async function saveAndRefreshPreview() {
  try {
    await fetch("/api/config", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(currentState)
    });
    const ts = Date.now();
    document.getElementById("previewImg").src = "/api/preview.gif?t=" + ts;
    document.getElementById("dlJpg").href = "/api/preview.jpg?download=1&t=" + ts;
    document.getElementById("dlGif").href = "/api/preview.gif?download=1&t=" + ts;
  } catch (e) {
    console.error(e);
  }
}

async function uploadCustomMedia() {
  const input = document.getElementById("mediaFileInput");
  if (!input.files || !input.files[0]) return;
  const file = input.files[0];
  setStatus("⏳ 이미지/움짤 파일을 불러오는 중...", "info");
  const reader = new FileReader();
  reader.onload = async () => {
    const base64Data = reader.result.split(",")[1];
    const resp = await fetch("/api/upload_media", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({filename: file.name, data_b64: base64Data})
    });
    const res = await resp.json();
    if (res.ok) {
      setStatus("✅ 내 이미지/움짤 적용 완료! 미리보기를 확인해보세요.", "ok");
      saveAndRefreshPreview();
    } else {
      setStatus("❌ 파일 업로드 오류: " + (res.error || ""), "err");
    }
  };
  reader.readAsDataURL(file);
}

async function scanDevices() {
  setStatus("🔍 같은 Wi-Fi 네트워크에서 긱매직 시계를 찾고 있습니다 (약 2~4초 소요)...", "info");
  try {
    const resp = await fetch("/api/discover");
    const data = await resp.json();
    if (data.devices && data.devices.length > 0) {
      const dev = data.devices[0];
      document.getElementById("deviceIp").value = dev.ip;
      onControlChange();
      setStatus(`🎉 긱매직 시계를 찾았습니다! IP: <b>${dev.ip}</b> (${dev.model || 'SmallTV'})`, "ok");
    } else {
      const subStr = (data.subnets || []).join(", ");
      setStatus(`⚠️ 자동 탐색에서 시계를 찾지 못했습니다 (스캔 대역: ${subStr || '로컬 네트워크'}).<br>시계 화면에 표시된 IP 주소를 직접 입력칸에 적어주세요!`, "err");
    }
  } catch (e) {
    setStatus("❌ 네트워크 스캔 중 오류가 발생했습니다: " + e, "err");
  }
}

async function pushToClock(asGif) {
  readFormIntoState();
  setStatus(asGif ? "🎞️ 움직이는 치이카와 GIF를 생성하여 시계로 전송 중..." : "🚀 치이카와 화면을 시계로 전송하고 있습니다...", "info");
  try {
    const resp = await fetch("/api/push", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({config: currentState, as_gif: asGif})
    });
    const res = await resp.json();
    if (res.ip && !document.getElementById("deviceIp").value.trim()) {
      document.getElementById("deviceIp").value = res.ip;
      currentState.device_ip = res.ip;
    }
    if (res.ok) {
      setStatus(`🎉 시계(${res.ip})로 전송 완료! (${(res.bytes/1024).toFixed(1)} KB · ${res.filename})`, "ok");
    } else {
      setStatus(`❌ 전송 실패: ${res.error}`, "err");
    }
  } catch (e) {
    setStatus("❌ 요청 중 오류 발생: " + e, "err");
  }
}

async function setClockBrightness(val) {
  const brt = parseInt(val, 10);
  document.getElementById("brtSlider").value = brt;
  document.getElementById("brtLabel").innerText = brt + "%";
  currentState.brightness = brt;
  readFormIntoState();
  setStatus(`🔆 시계 화면 밝기를 ${brt}%로 조절 중...`, "info");
  try {
    const resp = await fetch("/api/brightness", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify({device_ip: currentState.device_ip, brightness: brt})
    });
    const res = await resp.json();
    if (res.ok) {
      setStatus(`✅ 시계(${res.ip}) 화면 밝기가 <b>${brt}%</b>로 변경되었습니다!`, "ok");
    } else {
      setStatus(`❌ 밝기 변경 실패: ${res.error}`, "err");
    }
  } catch (e) {
    setStatus("❌ 밝기 변경 요청 오류: " + e, "err");
  }
}

async function shutdownServer() {
  if (!confirm("치이카와 시계 스튜디오 백그라운드 서버를 종료하시겠습니까?")) return;
  try {
    await fetch("/api/shutdown", {method: "POST"});
  } catch (e) {}
  document.body.innerHTML = "<div style='display:flex;height:100vh;align-items:center;justify-content:center;font-family:sans-serif;flex-direction:column;gap:12px;'><h2>👋 치이카와 시계 스튜디오가 종료되었습니다.</h2><p style='color:#666;'>이 브라우저 탭을 닫으셔도 됩니다.</p></div>";
}

async function initStudio() {
  const resp = await fetch("/api/config");
  const cfg = await resp.json();
  currentState = Object.assign(currentState, cfg);
  document.getElementById("deviceIp").value = currentState.device_ip || "";
  document.getElementById("layoutMode").value = currentState.layout || "character_focus";
  document.getElementById("cityKey").value = currentState.city_key || "seoul";
  document.getElementById("use12h").value = currentState.use_12h ? "true" : "false";
  document.getElementById("useManualWeather").checked = !!currentState.use_manual_weather;
  document.getElementById("manualTemp").value = currentState.manual_temp ?? 18;
  document.getElementById("manualCode").value = currentState.manual_code || "sunny";
  document.getElementById("customText").value = currentState.custom_text ?? "";
  document.getElementById("ddayTitle").value = currentState.dday_title ?? "";
  document.getElementById("ddayDate").value = currentState.dday_date ?? "";
  document.getElementById("autoSyncToggle").checked = !!currentState.auto_sync;
  const brt = currentState.brightness ?? 80;
  document.getElementById("brtSlider").value = brt;
  document.getElementById("brtLabel").innerText = brt + "%";
  updateChipUI();
}

initStudio();
setInterval(() => {
  const ts = Date.now();
  document.getElementById("previewImg").src = "/api/preview.gif?t=" + ts;
}, 30000);
</script>
</body>
</html>
"""


class StudioRequestHandler(BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: Any) -> None:
        pass  # Keep console clean

    def _send_json(self, data: Dict[str, Any], status: int = 200) -> None:
        raw = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path

        if path == "/" or path == "/index.html":
            raw = STUDIO_HTML.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return

        if path == "/api/config":
            cfg = load_config()
            if cfg.get("device_ip"):
                live_brt = get_device_brightness(cfg["device_ip"])
                if live_brt is not None:
                    cfg["brightness"] = live_brt
            self._send_json(cfg)
            return

        if path == "/api/discover":
            res = discover_geekmagic_devices()
            self._send_json(res)
            return

        if path == "/api/preview.jpg":
            cfg = load_config()
            weather = resolve_weather_for_config(cfg)
            custom_bytes = get_custom_media_bytes(cfg)
            jpg = render_clock_jpeg(cfg, weather, custom_image_bytes=custom_bytes)
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(jpg)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(jpg)
            return

        if path == "/api/preview.gif":
            cfg = load_config()
            weather = resolve_weather_for_config(cfg)
            custom_bytes = get_custom_media_bytes(cfg)
            gif = render_clock_gif(cfg, weather, custom_image_bytes=custom_bytes)
            self.send_response(200)
            self.send_header("Content-Type", "image/gif")
            self.send_header("Content-Length", str(len(gif)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(gif)
            return

        self.send_error(404)

    def do_POST(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get("Content-Length", "0"))
        body_raw = self.rfile.read(length) if length > 0 else b"{}"

        try:
            payload = json.loads(body_raw.decode("utf-8"))
        except Exception:
            payload = {}

        if path == "/api/config":
            cfg = load_config()
            saved_ip = cfg.get("device_ip", "")
            cfg.update(payload)
            if not cfg.get("device_ip") and saved_ip:
                cfg["device_ip"] = saved_ip
            save_config(cfg)
            self._send_json({"ok": True, "config": cfg})
            return

        if path == "/api/upload_media":
            filename = os.path.basename(payload.get("filename") or "custom.gif")
            b64 = payload.get("data_b64") or ""
            try:
                raw_bytes = base64.b64decode(b64)
                save_path = os.path.join(CUSTOM_MEDIA_DIR, filename)
                with open(save_path, "wb") as f:
                    f.write(raw_bytes)
                cfg = load_config()
                cfg["character"] = "custom"
                cfg["custom_media_filename"] = filename
                save_config(cfg)
                self._send_json({"ok": True, "filename": filename})
            except Exception as e:
                self._send_json({"ok": False, "error": str(e)}, status=400)
            return

        if path == "/api/shutdown":
            self._send_json({"ok": True})
            threading.Timer(0.3, lambda: os._exit(0)).start()
            return

        if path == "/api/brightness":
            cfg = load_config()
            ip = (payload.get("device_ip") or cfg.get("device_ip") or "").strip()
            brt = int(payload.get("brightness", 80))
            cfg["brightness"] = brt
            if ip:
                cfg["device_ip"] = ip
            save_config(cfg)
            res = control_device(ip, brightness=brt)
            self._send_json(res)
            return

        if path == "/api/push":
            new_cfg = payload.get("config")
            as_gif = bool(payload.get("as_gif", True))
            cfg = load_config()
            saved_ip = cfg.get("device_ip", "")
            if isinstance(new_cfg, dict):
                cfg.update(new_cfg)
            if not cfg.get("device_ip") and saved_ip:
                cfg["device_ip"] = saved_ip
            cfg["as_gif"] = as_gif
            save_config(cfg)
            res = build_and_push_now(cfg, as_gif=as_gif)
            self._send_json(res)
            return

        self.send_error(404)


def main() -> None:
    parser = argparse.ArgumentParser(description="GeekMagic Chiikawa Custom Screen Studio")
    parser.add_argument("--port", type=int, default=8765, help="Web Studio port (default: 8765)")
    parser.add_argument("--no-browser", action="store_true", help="Do not auto-open browser")
    parser.add_argument("--daemon", action="store_true", help="Run headless auto-sync loop in terminal")
    parser.add_argument("--ip", type=str, default="", help="GeekMagic device IP address")
    args = parser.parse_args()

    if args.ip:
        cfg = load_config()
        cfg["device_ip"] = args.ip.strip()
        save_config(cfg)

    if args.daemon:
        cfg = load_config()
        if not cfg.get("device_ip"):
            print("🔍 기기 IP가 설정되지 않아 로컬 네트워크에서 긱매직 시계를 탐색합니다...")
            disc = discover_geekmagic_devices()
            if disc["devices"]:
                cfg["device_ip"] = disc["devices"][0]["ip"]
                save_config(cfg)
                print(f"✅ 시계 발견! IP: {cfg['device_ip']}")
            else:
                print("⚠️ 시계를 자동으로 찾지 못했습니다. --ip <시계IP> 옵션을 함께 넣어주세요.")
                sys.exit(1)

        print(f"🐹 치이카와 시계 자동 동기화 데몬 시작 (대상 IP: {cfg['device_ip']})")
        while True:
            res = build_and_push_now(load_config(), as_gif=False)
            now_str = datetime.now().strftime("%H:%M:%S")
            if res.get("ok"):
                print(f"[{now_str}] ✅ 화면 업데이트 성공 ({res.get('bytes')} bytes)")
            else:
                print(f"[{now_str}] ⚠️ 전송 실패: {res.get('error')}")
            time.sleep(60)

    # Start background auto-sync worker thread
    worker = threading.Thread(target=auto_sync_worker, daemon=True)
    worker.start()

    url = f"http://127.0.0.1:{args.port}"
    server = ThreadingHTTPServer(("127.0.0.1", args.port), StudioRequestHandler)
    print("=" * 62)
    print("  🐹✨ 치이카와 x 긱매직 시계 커스텀 스튜디오가 실행되었습니다!")
    print(f"  👉 브라우저 주소: {url}")
    print("  (종료하려면 터미널에서 Ctrl+C 를 누르세요)")
    print("=" * 62)

    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n👋 스튜디오를 종료합니다.")


if __name__ == "__main__":
    main()
