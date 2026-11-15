"""Serve the unchanged ESP32 settings page as deterministic gzip bytes."""
import gzip
from pathlib import Path
import re

Import("env")

project = Path(env["PROJECT_DIR"])
source = (project / "src" / "web_ui.h").read_text(encoding="utf-8")
match = re.search(r'R"rawliteral\((.*)\)rawliteral";', source, re.DOTALL)
if not match:
    raise RuntimeError("Cannot locate WEB_INDEX_HTML raw literal")
payload = gzip.compress(match.group(1).encode("utf-8"), compresslevel=9, mtime=0)
rows = [", ".join(f"0x{value:02x}" for value in payload[i:i + 16])
        for i in range(0, len(payload), 16)]
header = ("#pragma once\n#include <Arduino.h>\n"
          "// Generated from src/web_ui.h by tools/pack_web.py.\n"
          "static const uint8_t WEB_INDEX_GZIP[] PROGMEM = {\n    "
          + ",\n    ".join(rows) + "\n};\n")
directory = project / ".pio" / "generated"
directory.mkdir(parents=True, exist_ok=True)
output = directory / "web_ui_gzip.h"
if not output.exists() or output.read_text() != header:
    output.write_text(header)
env.Append(CPPPATH=[str(directory)])
