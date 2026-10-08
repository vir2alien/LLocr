#!/usr/bin/env bash
# Полный сброс настроек LLocr на Linux (Qt: org "llocr", app "LLM OCR").
# Удаляет: QSettings, корень рантайма с моделями, кэши, данные WebEngine
# и следы тестовых org-идентификаторов (llocr_test, llocr-tests).
#
# Использование: reset-settings-linux.sh [-y]

set -euo pipefail

ORG="llocr"
TEST_ORGS=("llocr_test" "llocr-tests")

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

TARGETS=()
for org in "$ORG" "${TEST_ORGS[@]}"; do
    TARGETS+=("$CONFIG_DIR/$org" "$DATA_DIR/$org" "$CACHE_DIR/$org" "$STATE_DIR/$org")
done

echo "Будут удалены (настройки, кэши, скачанный рантайм и модели):"
printf '  %s\n' "${TARGETS[@]}"

if ! $ASSUME_YES; then
    read -r -p "Продолжить? [y/N] " answer
    case "$answer" in
        y|Y|yes|YES) ;;
        *) echo "Отменено."; exit 1 ;;
    esac
fi

for dir in "${TARGETS[@]}"; do
    if [ -e "$dir" ]; then
        rm -rf "$dir"
        echo "Удалено: $dir"
    fi
done

echo "Готово: настройки LLocr полностью удалены."
