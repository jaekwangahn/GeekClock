#!/bin/zsh
cd "$(dirname "$0")"

find_python() {
  for candidate in \
    "$HOME/.geekclock_env/bin/python3" \
    "$HOME/.pyenv/versions/3.12.11/bin/python3" \
    "$HOME/.pyenv/shims/python3" \
    "/opt/homebrew/bin/python3" \
    "/usr/local/bin/python3" \
    "/usr/bin/python3" \
    "$(which python3 2>/dev/null)"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
      if "$candidate" -c "import PIL" >/dev/null 2>&1; then
        echo "$candidate"
        return 0
      fi
    fi
  done

  # Fallback: bootstrap ~/.geekclock_env with Pillow
  BASE_PY="/opt/homebrew/bin/python3"
  [ ! -x "$BASE_PY" ] && BASE_PY="/usr/local/bin/python3"
  [ ! -x "$BASE_PY" ] && BASE_PY="/usr/bin/python3"

  echo "📦 처음 실행 환경을 준비하고 있습니다 (Pillow 설치 중)..." >&2
  "$BASE_PY" -m venv "$HOME/.geekclock_env"
  "$HOME/.geekclock_env/bin/pip" install --quiet --upgrade pip pillow
  echo "$HOME/.geekclock_env/bin/python3"
}

PY="$(find_python)"
exec /usr/bin/caffeinate -i "$PY" studio.py
