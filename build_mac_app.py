#!/usr/bin/env python3
"""
Builds a portable macOS Application Bundle ('GeekClock Studio.app')
and a distributable archive ('GeekClock_Studio_Mac.zip') that can run on any MacBook.
"""

from __future__ import annotations
import os
import plistlib
import shutil
import stat
import subprocess
from PIL import Image, ImageDraw
from chiikawa_art import draw_single_character, load_font

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
APP_NAME = "GeekClock Studio.app"
APP_PATH = os.path.join(BASE_DIR, APP_NAME)
ZIP_PATH = os.path.join(BASE_DIR, "GeekClock_Studio_Mac.zip")


def create_app_icon(icns_output_path: str) -> None:
    """Renders a 1024x1024 macOS squircle Chiikawa icon and compiles it to .icns via iconutil."""
    size = 1024
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    # macOS rounded squircle background with pastel pink-to-cream gradient
    margin = 64
    card = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cdraw = ImageDraw.Draw(card)
    cdraw.rounded_rectangle(
        [margin, margin, size - margin, size - margin],
        radius=210,
        fill=(255, 228, 238, 255),
        outline=(255, 148, 184, 255),
        width=22,
    )
    img.alpha_composite(card)
    draw = ImageDraw.Draw(img)

    # Top mini clock pill inside icon
    draw.rounded_rectangle(
        [180, 140, size - 180, 310],
        radius=64,
        fill=(255, 255, 255, 240),
        outline=(255, 178, 204, 255),
        width=10,
    )
    font_clock = load_font(112, weight="heavy", rounded_digits=True)
    tb = draw.textbbox((0, 0), "12:30", font=font_clock)
    tw = tb[2] - tb[0]
    draw.text(((size - tw) // 2, 166), "12:30", font=font_clock, fill=(68, 46, 56))

    # Draw Chiikawa large in center-bottom of icon
    draw_single_character(draw, "chiikawa", cx=size // 2, cy=645, scale=2.75, mood="happy", frame_idx=0)

    iconset_dir = os.path.join(BASE_DIR, "AppIcon.iconset")
    if os.path.exists(iconset_dir):
        shutil.rmtree(iconset_dir)
    os.makedirs(iconset_dir, exist_ok=True)

    icon_sizes = [16, 32, 128, 256, 512]
    for s in icon_sizes:
        im_1x = img.resize((s, s), Image.Resampling.LANCZOS)
        im_1x.save(os.path.join(iconset_dir, f"icon_{s}x{s}.png"))
        im_2x = img.resize((s * 2, s * 2), Image.Resampling.LANCZOS)
        im_2x.save(os.path.join(iconset_dir, f"icon_{s}x{s}@2x.png"))

    subprocess.run(["/usr/bin/iconutil", "-c", "icns", iconset_dir, "-o", icns_output_path], check=True)
    shutil.rmtree(iconset_dir, ignore_errors=True)


LAUNCHER_SCRIPT = r"""#!/bin/zsh
# GeekClock Studio macOS App Launcher
export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

CONTENTS_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RES_DIR="$CONTENTS_DIR/Resources"
APP_DIR="$RES_DIR/app"
VENDOR_DIR="$RES_DIR/vendor"

export GEEKCLOCK_DATA_DIR="$HOME/Library/Application Support/GeekClockStudio"
mkdir -p "$GEEKCLOCK_DATA_DIR"
mkdir -p "$HOME/Library/Logs"
LOG_FILE="$HOME/Library/Logs/GeekClockStudio.log"

# Seed default config.json on first launch if not present
if [ ! -f "$GEEKCLOCK_DATA_DIR/config.json" ] && [ -f "$APP_DIR/config.json" ]; then
  cp "$APP_DIR/config.json" "$GEEKCLOCK_DATA_DIR/config.json"
fi

# If server is already running on 8765, just open the browser UI!
if /usr/bin/curl -s --max-time 0.8 http://127.0.0.1:8765/api/config >/dev/null 2>&1; then
  /usr/bin/open "http://127.0.0.1:8765"
  exit 0
fi

find_ready_python() {
  # 1. Check existing virtualenv or system/homebrew/pyenv pythons that already have PIL
  for py in \
    "$HOME/.geekclock_env/bin/python3" \
    "$HOME/.pyenv/versions/3.12.11/bin/python3" \
    "$HOME/.pyenv/shims/python3" \
    "/opt/homebrew/bin/python3" \
    "/opt/homebrew/bin/python3.12" \
    "/usr/local/bin/python3" \
    "/usr/bin/python3"; do
    if [ -x "$py" ]; then
      if "$py" -c "import PIL" >/dev/null 2>&1; then
        echo "$py"
        return 0
      fi
      # Try with bundled vendor PIL packages (py39, py310, py312)
      ver="$("$py" -c 'import sys; print(f"py{sys.version_info.major}{sys.version_info.minor}")' 2>/dev/null)"
      if [ -n "$ver" ] && [ -d "$VENDOR_DIR/$ver" ]; then
        if PYTHONPATH="$VENDOR_DIR/$ver" "$py" -c "import PIL" >/dev/null 2>&1; then
          export PYTHONPATH="$VENDOR_DIR/$ver:$PYTHONPATH"
          echo "$py"
          return 0
        fi
      fi
    fi
  done

  # 2. Auto-bootstrap ~/.geekclock_env on a brand-new MacBook
  /usr/bin/osascript -e 'display notification "처음 실행 환경을 자동으로 준비하고 있습니다 (약 10초 소요)..." with title "치이카와 시계 스튜디오"' >/dev/null 2>&1 || true
  BASE_PY="/opt/homebrew/bin/python3"
  [ ! -x "$BASE_PY" ] && BASE_PY="/usr/local/bin/python3"
  [ ! -x "$BASE_PY" ] && BASE_PY="/usr/bin/python3"

  "$BASE_PY" -m venv "$HOME/.geekclock_env" >>"$LOG_FILE" 2>&1
  "$HOME/.geekclock_env/bin/pip" install --quiet --upgrade pip pillow >>"$LOG_FILE" 2>&1
  if [ -x "$HOME/.geekclock_env/bin/python3" ] && "$HOME/.geekclock_env/bin/python3" -c "import PIL" >/dev/null 2>&1; then
    echo "$HOME/.geekclock_env/bin/python3"
    return 0
  fi
  return 1
}

PY_BIN="$(find_ready_python)"
if [ -z "$PY_BIN" ]; then
  /usr/bin/osascript -e 'display alert "Python3 또는 Pillow 라이브러리를 찾을 수 없습니다." message "터미널에서 python3 -m pip install pillow 실행 후 다시 시도해주세요."'
  exit 1
fi

# Double-check if PYTHONPATH needs vendor dir for the selected python
ver="$("$PY_BIN" -c 'import sys; print(f"py{sys.version_info.major}{sys.version_info.minor}")' 2>/dev/null)"
if [ -n "$ver" ] && [ -d "$VENDOR_DIR/$ver" ]; then
  export PYTHONPATH="$VENDOR_DIR/$ver:$PYTHONPATH"
fi

cd "$APP_DIR"
nohup "$PY_BIN" "$APP_DIR/studio.py" >>"$LOG_FILE" 2>&1 &
exit 0
"""


def build_app() -> None:
    if os.path.exists(APP_PATH):
        shutil.rmtree(APP_PATH)

    macos_dir = os.path.join(APP_PATH, "Contents", "MacOS")
    res_dir = os.path.join(APP_PATH, "Contents", "Resources")
    app_code_dir = os.path.join(res_dir, "app")
    vendor_dir = os.path.join(res_dir, "vendor")

    os.makedirs(macos_dir, exist_ok=True)
    os.makedirs(app_code_dir, exist_ok=True)
    os.makedirs(vendor_dir, exist_ok=True)

    # 1. Create Info.plist
    info_plist = {
        "CFBundleName": "GeekClock Studio",
        "CFBundleDisplayName": "치이카와 시계 스튜디오",
        "CFBundleIdentifier": "com.geekclock.chiikawa.studio",
        "CFBundleVersion": "1.2.0",
        "CFBundleShortVersionString": "1.2.0",
        "CFBundlePackageType": "APPL",
        "CFBundleExecutable": "GeekClockStudio",
        "CFBundleIconFile": "AppIcon",
        "LSMinimumSystemVersion": "11.0",
        "NSHighResolutionCapable": True,
        "NSAppTransportSecurity": {"NSAllowsArbitraryLoads": True},
    }
    with open(os.path.join(APP_PATH, "Contents", "Info.plist"), "wb") as f:
        plistlib.dump(info_plist, f)

    # 2. Generate AppIcon.icns
    create_app_icon(os.path.join(res_dir, "AppIcon.icns"))

    # 3. Copy application Python files
    for fn in ("studio.py", "chiikawa_art.py", "geekmagic_client.py", "config.json"):
        src = os.path.join(BASE_DIR, fn)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(app_code_dir, fn))

    # 4. Bundle pre-built PIL (Pillow) for Python 3.9 (macOS default `/usr/bin/python3`), 3.10, and 3.12
    pyenv_map = {
        "py39": os.path.expanduser("~/.pyenv/versions/3.9.22/lib/python3.9/site-packages/PIL"),
        "py310": os.path.expanduser("~/.pyenv/versions/3.10.2/lib/python3.10/site-packages/PIL"),
        "py312": os.path.expanduser("~/.pyenv/versions/3.12.11/lib/python3.12/site-packages/PIL"),
    }
    for tag, pil_path in pyenv_map.items():
        if os.path.exists(pil_path):
            target_v = os.path.join(vendor_dir, tag, "PIL")
            shutil.copytree(pil_path, target_v)

    # 5. Write executable launcher script
    launcher_path = os.path.join(macos_dir, "GeekClockStudio")
    with open(launcher_path, "w", encoding="utf-8") as f:
        f.write(LAUNCHER_SCRIPT)
    os.chmod(launcher_path, os.stat(launcher_path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    # 6. Ad-hoc sign the app bundle so macOS runs it cleanly
    subprocess.run(["/usr/bin/codesign", "--force", "--deep", "--sign", "-", APP_PATH], check=False)

    # 7. Package into a portable .zip preserving macOS bundle permissions & metadata
    if os.path.exists(ZIP_PATH):
        os.remove(ZIP_PATH)
    subprocess.run(
        ["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", APP_PATH, ZIP_PATH],
        check=True,
    )
    print(f"Built app: {APP_PATH}")
    print(f"Built zip: {ZIP_PATH} ({os.path.getsize(ZIP_PATH) / (1024*1024):.2f} MB)")


if __name__ == "__main__":
    build_app()
