#!/usr/bin/env bash
# Полный сброс настроек LLocr на macOS (Qt: org "llocr", app "LLM OCR").
# Удаляет: QSettings (домен "llocr.LLM OCR"), корень рантайма с моделями,
# кэши, данные WebEngine, saved state и следы тестовых org-идентификаторов.
#
# Использование: reset-settings-macos.sh [-y]

set -euo pipefail

ORG="llocr"
APP="LLM OCR"
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

LIB="$HOME/Library"

DIRS=(
    "$LIB/Application Support/$ORG"
    "$LIB/Caches/$ORG"
    "$LIB/WebKit/$ORG"
)

PLIST_GLOBS=("$LIB/Preferences/$ORG.$APP.plist")
for org in "${TEST_ORGS[@]}"; do
    DIRS+=("$LIB/Application Support/$org" "$LIB/Caches/$org")
    PLIST_GLOBS+=("$LIB/Preferences/$org.*.plist")
    if defaults read "$org.$APP" >/dev/null 2>&1; then
        defaults delete "$org.$APP"
    fi
done

DOMAIN="$ORG.$APP"

echo "Будут удалены (настройки, кэши, скачанный рантайм и модели):"
echo "  defaults-домен: $DOMAIN"
printf '  %s\n' "${DIRS[@]}" "${PLIST_GLOBS[@]}"

if ! $ASSUME_YES; then
    read -r -p "Продолжить? [y/N] " answer
    case "$answer" in
        y|Y|yes|YES) ;;
        *) echo "Отменено."; exit 1 ;;
    esac
fi

if defaults read "$DOMAIN" >/dev/null 2>&1; then
    defaults delete "$DOMAIN"
    echo "Удалён defaults-домен: $DOMAIN"
fi

# cfprefsd кэширует plists: после defaults delete файл может остаться.
for plist in "${PLIST_GLOBS[@]}"; do
    for f in $plist; do
        [ -e "$f" ] || continue
        rm -f "$f"
        echo "Удалено: $f"
    done
done
if [ -e "$LIB/Preferences/$ORG.$APP.plist" ] || [ -e "$LIB/Preferences/llocr_test.$APP.plist" ]; then
    killall cfprefsd 2>/dev/null || true
fi

for dir in "${DIRS[@]}"; do
    if [ -e "$dir" ]; then
        rm -rf "$dir"
        echo "Удалено: $dir"
    fi
done

for state in "$LIB/Saved Application State/$ORG.$APP.savedState" \
             "$LIB/Saved Application State/"$ORG.$APP*.savedState; do
    [ -e "$state" ] || continue
    rm -rf "$state"
    echo "Удалено: $state"
done

echo "Готово: настройки LLocr полностью удалены."
