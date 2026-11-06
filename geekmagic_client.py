"""
GeekMagic WiFi Weather Clock Client & Auto-Discovery + Open-Meteo Weather Service.
Works with stock firmware (SmallTV / SmallTV-Pro / SmallTV-Ultra).
"""

from __future__ import annotations
import concurrent.futures
import gzip
import json
import re
import socket
import subprocess
import time
import urllib.parse
import urllib.request
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

    for path in ("/space.json", "/app.json", "/v.json", "/city.json", "/"):
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
                    if any(k in data for k in ("ver", "version", "theme", "brt", "model", "m")):
                        info["model"] = str(data.get("model") or data.get("m") or "GeekMagic SmallTV-Ultra")
                        info["version"] = str(data.get("ver") or data.get("version") or "")
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


def set_smalltv_clock_colors(ip: str, theme_key: str = "pink") -> None:
    """Sets Hour, Minute, Second digital clock colors on SmallTV classic firmware."""
    hc, mc, sc = CLOCK_COLOR_PALETTES.get(theme_key, CLOCK_COLOR_PALETTES["pink"])
    q = f"hc={urllib.parse.quote(hc)}&mc={urllib.parse.quote(mc)}&sc={urllib.parse.quote(sc)}"
    try:
        urllib.request.urlopen(f"http://{ip}/set?{q}", timeout=4.0).read()
    except Exception:
        pass


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

    # Check if this device is Classic SmallTV (has /city.json or /upload.html)
    dev_info = probe_geekmagic_ip(ip, timeout=1.5)
    is_classic_smalltv = bool(dev_info and dev_info.get("variant") == "smalltv_classic")

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


def control_device(device_ip: str, theme: Optional[int] = None, brightness: Optional[int] = None) -> Dict[str, Any]:
    """Changes theme or screen brightness on the GeekMagic device."""
    ip = device_ip.strip().replace("http://", "").replace("https://", "").split("/")[0]
    results = {}
    try:
        if theme is not None:
            with urllib.request.urlopen(f"http://{ip}/set?theme={int(theme)}", timeout=3.0) as r:
                results["theme"] = r.status
        if brightness is not None:
            with urllib.request.urlopen(f"http://{ip}/set?brt={int(brightness)}", timeout=3.0) as r:
                results["brightness"] = r.status
        return {"ok": True, "results": results}
    except Exception as e:
        return {"ok": False, "error": str(e)}
