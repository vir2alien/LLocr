#!/bin/sh
# Полный сброс настроек LLocr на Linux (Qt: org "llocr", app "LLM OCR").
# Удаляет: QSettings, корень рантайма с моделями, кэши, данные WebEngine
# и следы тестовых org-идентификаторов (llocr_test, llocr-test, ...).
# POSIX sh (работает и под sh, и под bash).
#
# Использование: reset-settings-linux.sh [-y]

set -eu

ORG="llocr"

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

CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA_DIR="${XDG_DATA_HOME:-$HOME/.local/share}"
CACHE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}"
STATE_DIR="${XDG_STATE_HOME:-$HOME/.local/state}"

TARGETS=""
for path in "$CONFIG_DIR/$ORG"* \
            "$DATA_DIR/$ORG"* \
            "$CACHE_DIR/$ORG"* \
            "$STATE_DIR/$ORG"*
do
    [ -e "$path" ] || continue
    TARGETS="$TARGETS$path
"
done

if [ -z "$TARGETS" ]; then
    echo "Настройки LLocr не найдены — удалять нечего."
    exit 0
fi

echo "Будут удалены (настройки, кэши, скачанный рантайм и модели):"
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

while IFS= read -r path; do
    [ -n "$path" ] || continue
    rm -rf "$path"
    echo "Удалено: $path"
done <<EOF
$TARGETS
EOF

echo "Готово: настройки LLocr полностью удалены."
