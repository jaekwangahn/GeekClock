"""Pack the ESP32 settings page and standalone BLE remote deterministically."""
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

# The remote is a standalone asset: HTTP serves its instructions/download;
# Web Bluetooth itself requires a supported browser and a secure origin.
remote_source = project / "ble-remote.html"
if remote_source.exists():
    remote_payload = gzip.compress(remote_source.read_bytes(), compresslevel=9, mtime=0)
    remote_rows = [", ".join(f"0x{value:02x}" for value in remote_payload[i:i + 16])
                   for i in range(0, len(remote_payload), 16)]
    remote_header = ("#pragma once\n#include <Arduino.h>\n"
                     "// Generated from ble-remote.html by tools/pack_web.py.\n"
                     "static const uint8_t BLE_REMOTE_GZIP[] PROGMEM = {\n    "
                     + ",\n    ".join(remote_rows) + "\n};\n")
    remote_output = directory / "ble_remote_gzip.h"
    if not remote_output.exists() or remote_output.read_text() != remote_header:
        remote_output.write_text(remote_header)
env.Append(CPPPATH=[str(directory)])
