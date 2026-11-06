#!/bin/zsh
cd "$(dirname "$0")"

if [ -x "/Users/jaekwangahn/.pyenv/versions/3.12.11/bin/python3" ]; then
  PY="/Users/jaekwangahn/.pyenv/versions/3.12.11/bin/python3"
else
  PY="python3"
fi

"$PY" studio.py
