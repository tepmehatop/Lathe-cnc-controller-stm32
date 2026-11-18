# PlatformIO (extra_scripts = pre:tools/fw_version.py): версия прошивки в сборку — ELS_FW_VERSION.
# Из git: «v0.4.1» на теге релиза, «v0.4.1-7-g5b07c53» между релизами, «-dirty» — есть несохранённые правки.
# Без git (архив исходников) — «нет версии». Видна на экране «НАСТРОЙКИ» (дисплей и STM32).
import os, subprocess
Import('env')
root = env.subst('$PROJECT_DIR')
try:
    v = subprocess.run(['git', 'describe', '--tags', '--always', '--dirty'], cwd=root,
                       capture_output=True, text=True, timeout=10).stdout.strip()
except Exception:
    v = ''
v = v or os.environ.get('GITHUB_REF_NAME', '') or 'нет версии'
env.Append(CPPDEFINES=[('ELS_FW_VERSION', env.StringifyMacro(v))])
print('ELS_FW_VERSION =', v)
