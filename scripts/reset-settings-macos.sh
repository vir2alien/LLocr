#!/bin/sh
# Полный сброс настроек LLocr на macOS (Qt: org "llocr", app "LLM OCR").
# Qt образует defaults-домен "com.llocr.LLM OCR" (и "com.<org>.<app>" для
# тестовых org), поэтому чистятся ВСЕ домены/plists, содержащие "llocr":
# настройки, корень рантайма с моделями, кэши, WebEngine, saved state.
# POSIX sh (работает и под sh, и под bash).
#
# Использование: reset-settings-macos.sh [-y]

set -eu

ORG="llocr"
LIB="$HOME/Library"

if pgrep -x "$ORG" >/dev/null 2>&1; then
    echo "Ошибка: LLocr запущен. Закройте приложение и повторите." >&2
    exit 1
fi

ASSUME_YES=false
case "${1:-}" in
    -y|--yes) ASSUME_YES=true ;;
    "") ;;
    *) echo "Использование: $0 [-y|--yes]" >&2; exit 2 ;;
esac

TARGETS=""
for path in "$LIB/Application Support/$ORG"* \
            "$LIB/Caches/$ORG"* \
            "$LIB/WebKit/$ORG"* \
            "$LIB/HTTPStorages/$ORG"* \
            "$LIB/Saved Application State/"*"llocr"*.savedState \
            "$LIB/Preferences/"*"llocr"*.plist
do
    [ -e "$path" ] || continue
    TARGETS="$TARGETS$path
"
done

DOMAINS=""
domain_list=$(defaults domains | tr ',' '\n' | sed 's/^[[:space:]]*//' | grep -i llocr || true)
while IFS= read -r domain; do
    [ -n "$domain" ] || continue
    DOMAINS="$DOMAINS$domain
"
done <<EOF
$domain_list
EOF

if [ -z "$TARGETS" ] && [ -z "$DOMAINS" ]; then
    echo "Настройки LLocr не найдены — удалять нечего."
    exit 0
fi

echo "Будут удалены (настройки, кэши, скачанный рантайм и модели):"
while IFS= read -r domain; do
    [ -n "$domain" ] || continue
    echo "  defaults-домен: $domain"
done <<EOF
$DOMAINS
EOF
while IFS= read -r path; do
    [ -n "$path" ] || continue
    echo "  $path"
done <<EOF
$TARGETS
EOF

if ! $ASSUME_YES; then
    printf 'Продолжить? [y/N] '
    read -r answer
    case "$answer" in
        y|Y|yes|YES) ;;
        *) echo "Отменено."; exit 1 ;;
    esac
fi

# Пустой домен (plist-файл {} после тестов) cfprefsd не считает существующим —
# отказ defaults delete для него нормален, сам файл вычистится ниже по glob'у.
while IFS= read -r domain; do
    [ -n "$domain" ] || continue
    if defaults delete "$domain" >/dev/null 2>&1; then
        echo "Удалён defaults-домен: $domain"
    fi
done <<EOF
$DOMAINS
EOF

if [ -n "$DOMAINS" ]; then
    # cfprefsd кэширует домены в памяти и может воссоздать plist из кэша.
    killall cfprefsd 2>/dev/null || true
fi

while IFS= read -r path; do
    [ -n "$path" ] || continue
    rm -rf "$path"
    echo "Удалено: $path"
done <<EOF
$TARGETS
EOF

echo "Готово: настройки LLocr полностью удалены."
