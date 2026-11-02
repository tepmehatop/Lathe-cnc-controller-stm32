#!/usr/bin/env bash
# Проверка индексированных (staged) изменений на персональные данные перед коммитом.
#
# Что ищется в добавляемых строках и бинарных файлах:
#   - абсолютные домашние пути (/Users/<имя>/, /home/<имя>/, C:\Users\<имя>\)
#   - логин текущего пользователя и имя компьютера (whoami, hostname)
#   - SSID и пароли из локального ELS_Display/include/wifi_config.h
#   - строки из локального файла .pii_patterns (ФИО, email и т.п., по одной на строку)
# Запрещены к коммиту: CLAUDE.md, .claude/, wifi_config.h, .pii_patterns.
#
# Использование: tools/check_pii.sh   (запускать после git add, перед git commit)
# Код выхода 1 = найдены совпадения, коммит нужно исправить.

set -u
ROOT="$(git rev-parse --show-toplevel)" || exit 2
cd "$ROOT"

fail=0
report() { echo "PII: $1"; fail=1; }

staged="$(git diff --cached --name-only --diff-filter=ACMR)"

# 1. Запрещённые файлы
while IFS= read -r f; do
    case "$f" in
        CLAUDE.md|.claude/*|*wifi_config.h|.pii_patterns)
            report "запрещённый файл в индексе: $f" ;;
    esac
done <<< "$staged"

# 2. Шаблоны для поиска
patterns=()
u="$(whoami 2>/dev/null)";      [ -n "$u" ] && patterns+=("$u")
h="$(hostname -s 2>/dev/null)"; [ -n "$h" ] && patterns+=("$h")
if command -v scutil >/dev/null 2>&1; then
    c="$(scutil --get ComputerName 2>/dev/null)"; [ -n "$c" ] && patterns+=("$c")
fi
wc_file="ELS_Display/include/wifi_config.h"
if [ -f "$wc_file" ]; then
    while IFS= read -r v; do
        [ -n "$v" ] && patterns+=("$v")
    done < <(sed -nE 's/^#define[[:space:]]+WIFI_(STA_SSID|STA_PASS|AP_PASS)[[:space:]]+"([^"]+)".*/\2/p' "$wc_file")
fi
if [ -f .pii_patterns ]; then
    while IFS= read -r v; do
        v="${v%%#*}"; v="$(echo "$v" | xargs)"
        [ -n "$v" ] && patterns+=("$v")
    done < .pii_patterns
fi

# 3. Добавляемые строки текстовых файлов
added="$(git diff --cached -U0 --no-color --diff-filter=ACMR | grep -E '^\+[^+]' || true)"
home_re='/Users/[A-Za-z0-9._-]+/|/home/[A-Za-z0-9._-]+/|[Cc]:\\+Users\\+[A-Za-z0-9._-]+'

if echo "$added" | grep -qE "$home_re"; then
    report "абсолютный домашний путь:"
    echo "$added" | grep -E "$home_re" | head -5 | sed 's/^/    /'
fi
for p in ${patterns[@]+"${patterns[@]}"}; do
    [ "${#p}" -lt 4 ] && continue   # короткие шаблоны дают ложные срабатывания
    if echo "$added" | grep -qiF -- "$p"; then
        report "совпадение с локальным шаблоном (длина ${#p}) — см. git diff --cached"
    fi
done

# 4. Бинарные файлы (прошивки, картинки): строки внутри
while IFS= read -r f; do
    [ -n "$f" ] && [ -f "$f" ] || continue
    grep -qI . "$f" 2>/dev/null && continue
    s="$(strings "$f" 2>/dev/null)"
    echo "$s" | grep -qE "$home_re" && report "бинарный файл $f содержит домашний путь"
    for p in ${patterns[@]+"${patterns[@]}"}; do
        [ "${#p}" -lt 4 ] && continue
        echo "$s" | grep -qiF -- "$p" && report "бинарный файл $f содержит локальный шаблон"
    done
done <<< "$staged"

[ "$fail" -eq 0 ] && echo "PII: OK — персональных данных в индексе не найдено"
exit "$fail"
