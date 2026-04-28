#!/usr/bin/env python3
"""
ELS Display Screenshot Tool
Скачивает скриншот с дисплея по HTTP и открывает его.

Использование:
    python3 screenshot.py <IP>           # например: python3 screenshot.py 192.168.1.105
    python3 screenshot.py <IP> -o out.bmp
"""

import urllib.request
import sys
import os
import subprocess
from datetime import datetime

def main():
    if len(sys.argv) < 2 or sys.argv[1].startswith("-"):
        print("Использование: python3 screenshot.py <IP-адрес>")
        print("Пример:        python3 screenshot.py 192.168.1.105")
        sys.exit(1)

    host = sys.argv[1]
    url  = f"http://{host}/screenshot"

    out_file = None
    if "-o" in sys.argv:
        idx = sys.argv.index("-o")
        if idx + 1 < len(sys.argv):
            out_file = sys.argv[idx + 1]

    if not out_file:
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        out_file = f"screenshot_{ts}.bmp"

    print(f"Загружаю скриншот с {url} ...")
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            data = resp.read()
    except Exception as e:
        print(f"ОШИБКА: {e}")
        sys.exit(1)

    with open(out_file, "wb") as f:
        f.write(data)

    size_kb = len(data) / 1024
    print(f"Сохранено: {out_file}  ({size_kb:.1f} KB)")

    # Открыть системным просмотрщиком
    if sys.platform == "darwin":
        subprocess.Popen(["open", out_file])
    elif sys.platform == "win32":
        os.startfile(out_file)
    else:
        subprocess.Popen(["xdg-open", out_file])

if __name__ == "__main__":
    main()
