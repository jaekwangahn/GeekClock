"""
GeekMagic WiFi Weather Clock Client & Auto-Discovery + Open-Meteo Weather Service.
Works with stock firmware (SmallTV / SmallTV-Pro / SmallTV-Ultra).
"""

from __future__ import annotations
import concurrent.futures
import email.utils
import gzip
import json
import re
import socket
import struct
import subprocess
import time
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone
from typing import Any, Dict, List, Optional, Tuple

KOREAN_CITIES: Dict[str, Dict[str, Any]] = {
    "seoul": {"name": "서울", "lat": 37.5665, "lon": 126.9780},
    "pangyo": {"name": "판교", "lat": 37.3947, "lon": 127.1112},
    "suwon": {"name": "수원", "lat": 37.2636, "lon": 127.0286},
    "incheon": {"name": "인천", "lat": 37.4563, "lon": 126.7052},
    "goyang": {"name": "고양", "lat": 37.6584, "lon": 126.8320},
    "yongin": {"name": "용인", "lat": 37.2411, "lon": 127.1776},
    "busan": {"name": "부산", "lat": 35.1796, "lon": 129.0756},
    "daegu": {"name": "대구", "lat": 35.8714, "lon": 128.6014},
    "daejeon": {"name": "대전", "lat": 36.3504, "lon": 127.3845},
    "gwangju": {"name": "광주", "lat": 35.1595, "lon": 126.8526},
    "ulsan": {"name": "울산", "lat": 35.5384, "lon": 129.3114},
    "sejong": {"name": "세종", "lat": 36.4800, "lon": 127.2890},
    "cheongju": {"name": "청주", "lat": 36.6424, "lon": 127.4890},
    "jeonju": {"name": "전주", "lat": 35.8242, "lon": 127.1480},
    "chuncheon": {"name": "춘천", "lat": 37.8813, "lon": 127.7298},
    "jeju": {"name": "제주", "lat": 33.4996, "lon": 126.5312},
}

_WEATHER_CACHE: Dict[str, Tuple[float, Dict[str, Any]]] = {}
WEATHER_TTL_SEC = 600  # 10 minutes


def wmo_to_weather_code(wmo: int) -> Tuple[str, str]:
    """Maps Open-Meteo WMO code to our icon code and Korean description."""
    if wmo == 0:
        return "sunny", "맑음"
    if wmo in (1, 2):
        return "partly_cloudy", "구름조금"
    if wmo in (3, 45, 48):
        return "cloudy", "흐림"
    if wmo in (51, 53, 55, 56, 57, 61, 63, 65, 66, 67, 80, 81, 82, 95, 96, 99):
        return "rain", "비"
    if wmo in (71, 73, 75, 77, 85, 86):
        return "snow", "눈"
    return "sunny", "맑음"


def fetch_weather(
    city_key: str = "seoul",
    manual_temp: Optional[int] = None,
    manual_code: Optional[str] = None,
) -> Dict[str, Any]:
    """
    Fetches live temperature and weather condition for the selected city via Open-Meteo.
    If manual_temp is set or network is unavailable, returns manual/cached values safely.
    """
    city_info = KOREAN_CITIES.get(city_key, KOREAN_CITIES["seoul"])
    city_name = city_info["name"]

    if manual_temp is not None:
        code = manual_code or "sunny"
        label_map = {
            "sunny": "맑음",
            "partly_cloudy": "구름조금",
            "cloudy": "흐림",
            "rain": "비 내림",
            "snow": "눈 내림",
        }
        return {
            "city": city_name,
            "temp": int(manual_temp),
            "code": code,
            "summary": f"{label_map.get(code, '맑음')} · 수동설정",
            "source": "manual",
        }

    now_ts = time.time()
    cached = _WEATHER_CACHE.get(city_key)
    if cached and (now_ts - cached[0] < WEATHER_TTL_SEC):
        return cached[1]

    lat, lon = city_info["lat"], city_info["lon"]
    url = (
        f"https://api.open-meteo.com/v1/forecast?"
        f"latitude={lat}&longitude={lon}"
        f"&current=temperature_2m,relative_humidity_2m,weather_code"
        f"&daily=temperature_2m_max,temperature_2m_min"
        f"&timezone=Asia%2FSeoul&forecast_days=1"
    )
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "GeekMagicChiikawaStudio/1.0"})
        with urllib.request.urlopen(req, timeout=4.0) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        cur = data.get("current", {})
        daily = data.get("daily", {})
        temp = round(float(cur.get("temperature_2m", 18)))
        humidity = round(float(cur.get("relative_humidity_2m", 50)))
        wmo = int(cur.get("weather_code", 0))
        code, desc = wmo_to_weather_code(wmo)

        tmax_list = daily.get("temperature_2m_max", [])
        tmin_list = daily.get("temperature_2m_min", [])
        if tmax_list and tmin_list:
            summary = f"최고 {round(tmax_list[0])}° 최저 {round(tmin_list[0])}°"
        else:
            summary = f"{desc} · 습도 {humidity}%"

        result = {
            "city": city_name,
            "temp": temp,
            "code": code,
            "summary": summary,
            "humidity": humidity,
            "source": "open-meteo",
        }
        _WEATHER_CACHE[city_key] = (now_ts, result)
        return result
    except Exception:
        if cached:
            return cached[1]
        return {
            "city": city_name,
            "temp": 18,
            "code": manual_code or "sunny",
            "summary": "맑음 · 쾌적",
            "source": "fallback",
        }


def get_local_subnets() -> Tuple[List[str], List[str]]:
    """
    Returns (priority_arp_ips, subnet_prefixes) on the local Mac.
    """
    arp_ips: List[str] = []
    prefixes: List[str] = []
    my_ips: set[str] = set()

    try:
        out = subprocess.check_output(["ifconfig"], text=True, timeout=2.0)
        for m in re.finditer(r"inet\s+(\d+\.\d+\.\d+\.\d+)\s+netmask", out):
            ip = m.group(1)
            if not ip.startswith("127.") and not ip.startswith("169.254."):
                my_ips.add(ip)
                prefix = ".".join(ip.split(".")[:3])
                if prefix not in prefixes:
                    prefixes.append(prefix)
    except Exception:
        pass

    try:
        arp_out = subprocess.check_output(["arp", "-an"], text=True, timeout=2.0)
        for m in re.finditer(r"\((\d+\.\d+\.\d+\.\d+)\)", arp_out):
            ip = m.group(1)
            if ip not in my_ips and not ip.endswith(".255") and not ip.startswith("224."):
                arp_ips.append(ip)
    except Exception:
        pass

    return arp_ips, prefixes


def _read_http_text(resp: Any, limit: int = 4096) -> str:
    raw = resp.read(limit)
    if resp.headers.get("Content-Encoding", "").lower() == "gzip" or raw[:2] == b"\x1f\x8b":
        try:
            raw = gzip.decompress(raw)
        except Exception:
            pass
    return raw.decode("utf-8", errors="ignore")


def probe_geekmagic_ip(ip: str, timeout: float = 0.75) -> Optional[Dict[str, Any]]:
    """
    Checks if the target IP is a GeekMagic WiFi Weather Clock by probing its HTTP endpoints.
    Supports both Original SmallTV (/city.json, /upload) and SmallTV-Pro/Ultra (/space.json, /doUpload).
    """
    try:
        with socket.create_connection((ip, 80), timeout=timeout):
            pass
    except Exception:
        return None

    info: Dict[str, Any] = {"ip": ip, "model": "GeekMagic SmallTV", "variant": "smalltv", "free_kb": None}

    for path in ("/v.json", "/space.json", "/app.json", "/city.json", "/"):
        try:
            req = urllib.request.Request(f"http://{ip}{path}", headers={"User-Agent": "GeekMagicScanner/1.0"})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                raw = _read_http_text(resp, 4096)
                if path == "/space.json":
                    data = json.loads(raw)
                    if "free" in data or "total" in data:
                        info["free_kb"] = round(int(data.get("free", 0)) / 1024, 1)
                        info["total_kb"] = round(int(data.get("total", 0)) / 1024, 1)
                        info["model"] = "GeekMagic SmallTV-Ultra/Pro (240x240)"
                        info["variant"] = "ultra_pro"
                        return info
                elif path in ("/app.json", "/v.json"):
                    data = json.loads(raw)
                    if data.get("standalone"):
                        info["model"] = f"Chiikawa Standalone ({data.get('v', 'V1.0')})"
                        info["version"] = str(data.get("v") or "Chiikawa-Standalone-V1.0")
                        info["variant"] = "chiikawa_standalone"
                        info["standalone"] = True
                        return info
                    if any(k in data for k in ("ver", "version", "theme", "brt", "model", "m")):
                        info["model"] = str(data.get("model") or data.get("m") or "GeekMagic SmallTV-Ultra")
                        info["version"] = str(data.get("ver") or data.get("version") or data.get("v") or "")
                        info["variant"] = "ultra_pro"
                        return info
                elif path == "/city.json":
                    data = json.loads(raw)
                    if "ct" in data or "cd" in data:
                        info["model"] = f"GeekMagic SmallTV ({data.get('cd', 'Weather Clock')})"
                        info["variant"] = "smalltv_classic"
                        return info
                else:
                    low = raw.lower()
                    if any(kw in low for kw in ("geekmagic", "smalltv", "doupload", "weather clock", "holocubic", "theme")):
                        if "upload.html" in low or "setgif" in low:
                            info["variant"] = "smalltv_classic"
                        return info
        except Exception:
            continue

    return None


def discover_geekmagic_devices() -> Dict[str, Any]:
    """
    Scans the local network for GeekMagic clocks and returns found devices + scanned subnets.
    """
    arp_ips, prefixes = get_local_subnets()
    found: List[Dict[str, Any]] = []
    seen: set[str] = set()

    # 1. Check ARP cache neighbors first (very fast)
    if arp_ips:
        with concurrent.futures.ThreadPoolExecutor(max_workers=32) as ex:
            for res in ex.map(lambda ip: probe_geekmagic_ip(ip, timeout=0.9), arp_ips):
                if res and res["ip"] not in seen:
                    seen.add(res["ip"])
                    found.append(res)

    if found:
        return {"devices": found, "subnets": prefixes}

    # 2. Full /24 sweep on active local subnets
    candidate_ips: List[str] = []
    for prefix in prefixes[:2]:
        for host in range(1, 255):
            ip = f"{prefix}.{host}"
            if ip not in seen:
                candidate_ips.append(ip)

    if candidate_ips:
        with concurrent.futures.ThreadPoolExecutor(max_workers=64) as ex:
            for res in ex.map(lambda ip: probe_geekmagic_ip(ip, timeout=0.55), candidate_ips):
                if res and res["ip"] not in seen:
                    seen.add(res["ip"])
                    found.append(res)

    return {"devices": found, "subnets": prefixes}


def _build_multipart(field_name: str, filename: str, file_bytes: bytes, content_type: str) -> Tuple[bytes, str]:
    """Constructs a standard multipart/form-data payload compatible with ESP8266/ESP32 WebServer."""
    boundary = "----GeekMagicBoundary7MA4YWxkTrZu0gW"
    lines = [
        f"--{boundary}\r\n".encode("ascii"),
        f'Content-Disposition: form-data; name="{field_name}"; filename="{filename}"\r\n'.encode("utf-8"),
        f"Content-Type: {content_type}\r\n\r\n".encode("ascii"),
        file_bytes,
        f"\r\n--{boundary}--\r\n".encode("ascii"),
    ]
    body = b"".join(lines)
    header_ct = f"multipart/form-data; boundary={boundary}"
    return body, header_ct


CLOCK_COLOR_PALETTES: Dict[str, Tuple[str, str, str]] = {
    "pink": ("#FF85A2", "#FFFFFF", "#FFB8D0"),      # Chiikawa Peach Pink
    "sky": ("#58B2DC", "#FFFFFF", "#A2DCFF"),       # Hachiware Soda Blue
    "butter": ("#FFD166", "#FFF7DE", "#FFA552"),    # Usagi Custard Yellow
    "lavender": ("#C792EA", "#FFFFFF", "#89DDFF"),  # Momonga Lavender
    "night": ("#FFD166", "#B2C8FF", "#FF85A2"),     # Starlight Night
}


_DEVICE_VARIANT_CACHE: Dict[str, str] = {}


def set_smalltv_clock_colors(ip: str, theme_key: str = "pink") -> None:
    """Sets Hour, Minute, Second digital clock colors on SmallTV classic firmware."""
    hc, mc, sc = CLOCK_COLOR_PALETTES.get(theme_key, CLOCK_COLOR_PALETTES["pink"])
    q = f"hc={urllib.parse.quote(hc)}&mc={urllib.parse.quote(mc)}&sc={urllib.parse.quote(sc)}"
    try:
        urllib.request.urlopen(f"http://{ip}/set?{q}", timeout=4.0).read()
    except Exception:
        pass


def render_standalone_msg_bmp_hex(text: str) -> str:
    """Renders arbitrary Korean text to a 196x18 4-bit grayscale alpha hex string for Chiikawa Standalone firmware."""
    text = (text or "").strip()
    if not text:
        return ""
    try:
        from PIL import Image, ImageDraw
        import chiikawa_art as art
        W, H = 196, 18
        cv = Image.new("L", (W * 2, H * 2), 0)
        draw = ImageDraw.Draw(cv)
        font = art.load_font(24, weight="bold")
        for sz in (25, 23, 21, 19, 17):
            font = art.load_font(sz, weight="bold")
            tb = draw.textbbox((0, 0), text, font=font)
            if (tb[2] - tb[0]) <= W * 2 - 8:
                break
        tb = draw.textbbox((0, 0), text, font=font)
        tw, th = tb[2] - tb[0], tb[3] - tb[1]
        tx = max(4, (W * 2 - tw) // 2)
        ty = (H * 2 - th) // 2 - tb[1]
        draw.text((tx, ty), text, font=font, fill=255)
        sm = cv.resize((W, H), Image.Resampling.LANCZOS)
        px = list(sm.getdata())
        out = bytearray()
        for i in range(0, W * H, 2):
            a1 = min(15, (px[i] + 8) >> 4)
            a2 = min(15, (px[i + 1] + 8) >> 4)
            out.append((a1 << 4) | a2)
        return out.hex()
    except Exception:
        return ""


def push_standalone_config(device_ip: str, cfg: Dict[str, Any]) -> Dict[str, Any]:
    """Pushes configuration & Korean text bitmap directly to Chiikawa Standalone firmware (/api/config)."""
    ip = device_ip.strip().replace("http://", "").replace("https://", "").split("/")[0]
    city_key = cfg.get("city_key", "seoul")
    city_info = KOREAN_CITIES.get(city_key, KOREAN_CITIES["seoul"])
    city_label = (cfg.get("city_label") or "").strip() or city_info["name"]

    char_val = cfg.get("character", "chiikawa")
    if char_val not in ("chiikawa", "hachiware", "usagi", "trio"):
        char_val = "chiikawa"

    payload = {
        "character": char_val,
        "theme": cfg.get("theme", "pink"),
        "brightness": max(3, min(100, int(cfg.get("brightness", 80)))),
        "anim": 1 if cfg.get("as_gif", True) else 0,
        "use12h": 1 if cfg.get("use_12h", False) else 0,
        "showSec": 1,
        "ntpServer": (cfg.get("ntp_server") or "time.google.com").strip(),
        "city": city_label,
        "lat": float(city_info["lat"]),
        "lon": float(city_info["lon"]),
        "customText": (cfg.get("custom_text") or "").strip(),
        "ddayTitle": (cfg.get("dday_title") or "").strip(),
        "ddayDate": (cfg.get("dday_date") or "").strip(),
    }
    try:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        req = urllib.request.Request(
            f"http://{ip}/api/config",
            data=body,
            method="POST",
            headers={"Content-Type": "application/json; charset=utf-8"},
        )
        with urllib.request.urlopen(req, timeout=5.0) as r:
            _ = r.read()

        import chiikawa_art as art
        dday_str = art.calculate_dday(payload["ddayDate"])
        ctext = payload["customText"]
        dtitle = payload["ddayTitle"]
        if dday_str and ctext:
            full_line = f"[{dtitle + ' ' if dtitle else ''}{dday_str}] {ctext}"
        elif dday_str:
            full_line = f"{dtitle + ' ' if dtitle else ''}{dday_str}"
        else:
            full_line = ctext

        bmp_hex = render_standalone_msg_bmp_hex(full_line)
        req_bmp = urllib.request.Request(
            f"http://{ip}/api/msg_bmp",
            data=bmp_hex.encode("ascii"),
            method="POST",
            headers={"Content-Type": "text/plain"},
        )
        with urllib.request.urlopen(req_bmp, timeout=5.0) as r2:
            _ = r2.read()

        return {
            "ok": True,
            "ip": ip,
            "variant": "chiikawa_standalone",
            "standalone": True,
            "remote_path": "/api/config (독립형 내장 펌웨어)",
            "bytes": len(body),
        }
    except Exception as e:
        return {"ok": False, "error": f"독립형 시계({ip}) 설정 전송 실패: {e}"}


def push_to_geekmagic(
    device_ip: str,
    file_bytes: bytes,
    is_gif: bool = False,
    filename: Optional[str] = None,
    switch_to_album_theme: bool = True,
    smalltv_gif_bytes: Optional[bytes] = None,
    theme_key: str = "pink",
) -> Dict[str, Any]:
    """
    Uploads to GeekMagic clock and activates it on screen.
    Automatically handles both:
      1) Classic SmallTV firmware (/upload + /set?gif=10 + /set?hc=...&mc=...&sc=...)
      2) SmallTV-Pro / Ultra firmware (/doUpload?dir=/image/ + /set?theme=3 + /set?img=...)
    """
    ip = device_ip.strip().replace("http://", "").replace("https://", "").split("/")[0]
    if not ip:
        return {"ok": False, "error": "기기 IP 주소를 입력해주세요."}

    variant = _DEVICE_VARIANT_CACHE.get(ip)
    if not variant:
        dev_info = probe_geekmagic_ip(ip, timeout=3.5)
        variant = (dev_info or {}).get("variant", "ultra_pro")
        if dev_info:
            _DEVICE_VARIANT_CACHE[ip] = variant
    is_classic_smalltv = (variant == "smalltv_classic")

    if is_classic_smalltv and smalltv_gif_bytes is not None:
        # Classic SmallTV uses 80x80 animated GIF at POST /upload + GET /set?gif=10
        gif_name = "chiikawa.gif"
        body, header_ct = _build_multipart("file", gif_name, smalltv_gif_bytes, "image/gif")
        try:
            req = urllib.request.Request(
                f"http://{ip}/upload",
                data=body,
                method="POST",
                headers={
                    "Content-Type": header_ct,
                    "Content-Length": str(len(body)),
                    "Connection": "close",
                },
            )
            with urllib.request.urlopen(req, timeout=12.0) as resp:
                _ = resp.read()
            # Activate custom GIF slot 10
            urllib.request.urlopen(f"http://{ip}/set?gif=10", timeout=5.0).read()
            # Match Hour/Minute/Second colors to the selected Chiikawa theme
            set_smalltv_clock_colors(ip, theme_key=theme_key)
            return {
                "ok": True,
                "ip": ip,
                "variant": "smalltv_classic",
                "filename": gif_name,
                "remote_path": "/upload (gif=10)",
                "bytes": len(smalltv_gif_bytes),
            }
        except Exception as e:
            return {
                "ok": False,
                "error": f"SmallTV({ip}) GIF 업로드 실패: {e}",
            }

    if not filename:
        filename = "chiikawa.gif" if is_gif else "chiikawa.jpg"

    # IMPORTANT for SmallTV-Ultra (ESP8266):
    # Playing a full-screen 240x240 GIF holds the file open in LittleFS and uses ~85% CPU.
    # Pausing GIF playback (by switching to static JPG or deleting target file first) makes /doUpload 10x faster and prevents timeouts!
    try:
        if is_gif:
            urllib.request.urlopen(f"http://{ip}/set?img=/image/chiikawa.jpg", timeout=3.0).read()
        urllib.request.urlopen(f"http://{ip}/delete?file=/image/{urllib.parse.quote(filename)}", timeout=3.0).read()
    except Exception:
        pass

    content_type = "image/gif" if is_gif else "image/jpeg"
    body, header_ct = _build_multipart("file", filename, file_bytes, content_type)

    # Try uploading to /image/ (Ultra & Pro firmware) and fallback to /upload
    upload_paths = ["/doUpload?dir=/image/", "/doUpload?dir=/gif/", "/doUpload", "/upload"]
    uploaded_dir = "/image/"
    upload_ok = False
    used_classic_upload = False
    last_err = ""

    for up_path in upload_paths:
        url = f"http://{ip}{up_path}"
        try:
            req = urllib.request.Request(
                url,
                data=body,
                method="POST",
                headers={
                    "Content-Type": header_ct,
                    "Content-Length": str(len(body)),
                    "Connection": "close",
                },
            )
            with urllib.request.urlopen(req, timeout=20.0) as resp:
                _ = resp.read()
                if resp.status in (200, 201, 204):
                    upload_ok = True
                    if up_path == "/upload":
                        used_classic_upload = True
                    elif "dir=/gif/" in up_path:
                        uploaded_dir = "/gif/"
                    elif "dir=/image/" in up_path:
                        uploaded_dir = "/image/"
                    else:
                        uploaded_dir = "/"
                    break
        except Exception as e:
            last_err = str(e)

    if not upload_ok:
        return {
            "ok": False,
            "error": f"기기({ip}) 연결 또는 업로드 실패: {last_err}. 시계와 같은 Wi-Fi인지, IP가 맞는지 확인해주세요.",
        }

    if used_classic_upload:
        try:
            urllib.request.urlopen(f"http://{ip}/set?gif=10", timeout=5.0).read()
            set_smalltv_clock_colors(ip, theme_key=theme_key)
        except Exception:
            pass
        return {
            "ok": True,
            "ip": ip,
            "variant": "smalltv_classic",
            "filename": filename,
            "remote_path": "/upload (gif=10)",
            "bytes": len(file_bytes),
        }

    # Switch to Photo Album / Custom Display theme (theme=3 on SmallTV-Ultra/Pro) if requested
    if switch_to_album_theme:
        try:
            urllib.request.urlopen(f"http://{ip}/set?theme=3", timeout=3.0).read()
        except Exception:
            pass

    remote_path = f"{uploaded_dir.rstrip('/')}/{filename}"
    set_urls = [
        f"http://{ip}/set?img={urllib.parse.quote(remote_path)}",
        f"http://{ip}/set?img={urllib.parse.quote(filename)}",
    ]

    for s_url in set_urls:
        try:
            with urllib.request.urlopen(s_url, timeout=3.0) as resp:
                if resp.status == 200:
                    break
        except Exception:
            continue

    return {
        "ok": True,
        "ip": ip,
        "variant": "ultra_pro",
        "filename": filename,
        "remote_path": remote_path,
        "bytes": len(file_bytes),
    }


def control_device(
    device_ip: str,
    theme: Optional[int] = None,
    brightness: Optional[int] = None,
    night_mode: Optional[Dict[str, Any]] = None,
) -> Dict[str, Any]:
    """Changes theme, screen brightness (0~100), or night mode schedule on the GeekMagic device."""
    ip = device_ip.strip().replace("http://", "").replace("https://", "").split("/")[0]
    if not ip:
        return {"ok": False, "error": "기기 IP 주소가 없습니다."}
    results: Dict[str, Any] = {}
    try:
        if theme is not None:
            with urllib.request.urlopen(f"http://{ip}/set?theme={int(theme)}", timeout=4.0) as r:
                results["theme"] = r.status
        if brightness is not None:
            brt_val = max(0, min(250, int(brightness)))
            with urllib.request.urlopen(f"http://{ip}/set?brt={brt_val}", timeout=4.0) as r:
                results["brightness"] = brt_val
                results["status"] = r.status
        if night_mode is not None:
            t1 = int(night_mode.get("start_hour", 22))
            t2 = int(night_mode.get("end_hour", 7))
            b1 = max(0, min(250, int(night_mode.get("day_brt", brightness or 80))))
            b2 = max(0, min(250, int(night_mode.get("night_brt", 5))))
            en = 1 if night_mode.get("enabled") else 0
            url = f"http://{ip}/set?t1={t1}&t2={t2}&b1={b1}&b2={b2}&en={en}"
            with urllib.request.urlopen(url, timeout=4.0) as r:
                results["night_mode"] = r.status
        return {"ok": True, "ip": ip, "results": results}
    except Exception as e:
        return {"ok": False, "error": str(e)}


def get_device_brightness(device_ip: str) -> Optional[int]:
    """Reads current brightness from /brt.json on the GeekMagic device if available."""
    ip = device_ip.strip().replace("http://", "").replace("https://", "").split("/")[0]
    if not ip:
        return None
    try:
        with urllib.request.urlopen(f"http://{ip}/brt.json", timeout=2.5) as r:
            data = json.loads(_read_http_text(r, 1024))
            if "brt" in data:
                return int(data["brt"])
    except Exception:
        pass
    return None


# --- NTP Time Synchronization (SNTP UDP 123 + HTTPS Date fallback) ---
KST = timezone(timedelta(hours=9))
NTP_EPOCHS_DELTA = 2208988800  # 1900-01-01 to 1970-01-01 in seconds
NTP_TTL_SEC = 600  # Re-sync NTP offset every 10 minutes

_NTP_STATE: Dict[str, Any] = {
    "offset_sec": 0.0,
    "last_sync_mono": 0.0,
    "last_sync_str": "",
    "server": "time.google.com",
    "synced": False,
    "rtt_ms": 0.0,
}


def _query_sntp(host: str, timeout: float = 2.0) -> Tuple[float, float]:
    """Queries an SNTP server on UDP port 123 and returns (offset_seconds, rtt_ms)."""
    packet = b"\x1b" + 47 * b"\x00"
    t0 = time.time()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(timeout)
        sock.sendto(packet, (host, 123))
        data, _ = sock.recvfrom(1024)
    t1 = time.time()
    if len(data) < 48:
        raise ValueError("Invalid NTP response packet")
    unpacked = struct.unpack("!12I", data[:48])
    tx_sec = unpacked[10] - NTP_EPOCHS_DELTA
    tx_frac = unpacked[11] / float(1 << 32)
    ntp_ts = tx_sec + tx_frac
    rtt = t1 - t0
    offset = (ntp_ts + rtt / 2.0) - t1
    return offset, rtt * 1000.0


def sync_ntp_offset(preferred_server: str = "time.google.com", force: bool = False) -> Dict[str, Any]:
    """
    Synchronizes clock offset against time.google.com (with fallback NTP servers & HTTPS Date header).
    """
    server = (preferred_server or "time.google.com").strip()
    now_mono = time.monotonic()
    if (
        not force
        and _NTP_STATE["synced"]
        and _NTP_STATE["server"] == server
        and (now_mono - _NTP_STATE["last_sync_mono"] < NTP_TTL_SEC)
    ):
        return dict(_NTP_STATE)

    candidates = [server]
    for fb in ("time.google.com", "time.apple.com", "kr.pool.ntp.org", "time.cloudflare.com"):
        if fb not in candidates:
            candidates.append(fb)

    for host in candidates:
        try:
            offset, rtt_ms = _query_sntp(host, timeout=1.8)
            _NTP_STATE.update(
                {
                    "offset_sec": offset,
                    "last_sync_mono": now_mono,
                    "last_sync_str": datetime.fromtimestamp(time.time() + offset, tz=KST).strftime("%H:%M:%S"),
                    "server": host,
                    "synced": True,
                    "rtt_ms": round(rtt_ms, 1),
                }
            )
            return dict(_NTP_STATE)
        except Exception:
            continue

    # Fallback if UDP 123 is blocked by firewall: use HTTPS Date header from Google
    try:
        t0 = time.time()
        req = urllib.request.Request("https://www.google.com/generate_204", method="HEAD")
        with urllib.request.urlopen(req, timeout=2.5) as resp:
            date_hdr = resp.headers.get("Date")
        t1 = time.time()
        if date_hdr:
            srv_dt = email.utils.parsedate_to_datetime(date_hdr)
            offset = srv_dt.timestamp() - ((t0 + t1) / 2.0)
            _NTP_STATE.update(
                {
                    "offset_sec": offset,
                    "last_sync_mono": now_mono,
                    "last_sync_str": datetime.fromtimestamp(time.time() + offset, tz=KST).strftime("%H:%M:%S"),
                    "server": "google.com (HTTPS)",
                    "synced": True,
                    "rtt_ms": round((t1 - t0) * 1000.0, 1),
                }
            )
    except Exception:
        pass

    return dict(_NTP_STATE)


def get_ntp_now(preferred_server: str = "time.google.com") -> datetime:
    """
    Returns current KST (Asia/Seoul) naive datetime accurately adjusted by NTP server offset.
    Even if the Mac's local system clock is wrong or drifts, this returns true NTP time!
    """
    state = sync_ntp_offset(preferred_server=preferred_server, force=False)
    true_ts = time.time() + float(state.get("offset_sec", 0.0))
    return datetime.fromtimestamp(true_ts, tz=KST).replace(tzinfo=None)


def get_ntp_status() -> Dict[str, Any]:
    """Returns current NTP synchronization state for UI display."""
    state = dict(_NTP_STATE)
    true_ts = time.time() + float(state.get("offset_sec", 0.0))
    state["current_kst"] = datetime.fromtimestamp(true_ts, tz=KST).strftime("%Y-%m-%d %H:%M:%S")
    return state


def set_device_ntp(device_ip: str, ntp_server: str = "time.google.com") -> Dict[str, Any]:
    """Configures the GeekMagic hardware's internal NTP server (/set?ntp=...)."""
    ip = device_ip.strip().replace("http://", "").replace("https://", "").split("/")[0]
    srv = (ntp_server or "time.google.com").strip()
    if not ip:
        return {"ok": False, "error": "기기 IP 주소가 없습니다."}
    try:
        url = f"http://{ip}/set?ntp={urllib.parse.quote(srv)}"
        with urllib.request.urlopen(url, timeout=4.0) as r:
            return {"ok": True, "ip": ip, "ntp_server": srv, "status": r.status}
    except Exception as e:
        return {"ok": False, "error": str(e)}
