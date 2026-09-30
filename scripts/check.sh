#!/bin/sh
# One-command verification: configure → build → test → format (→ lint).
#
# This is the same sequence CI runs, so a green run here is a green CI run.
# It reuses an already-configured tree on purpose (AGENTS.md: do not
# re-configure build/ from scratch).
#
#   scripts/check.sh                  build + test (+ format if available)
#   scripts/check.sh --configure      re-configure the build tree first
#   scripts/check.sh --no-tests       build only
#   scripts/check.sh --no-format      skip the clang-format check
#   scripts/check.sh --no-lint        skip the QML lint
#   scripts/check.sh --format-only    clang-format check alone, no Qt needed
#
# Environment:
#   BUILD_DIR   build tree to use            (default: build)
#   JOBS        parallel build/test jobs     (default: 8, or nproc)
#   QT_PREFIX   CMAKE_PREFIX_PATH for a fresh configure
#                                        (default: ~/Qt/6.10.3/macos)
#   BUILD_TYPE  CMAKE_BUILD_TYPE             (default: Debug)
#
# Missing tools degrade to a notice, never to a silent pass: clang-format and
# qmllint are skipped with a warning when not installed (CI installs them and
# turns them into hard gates).

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
BUILD_TYPE=${BUILD_TYPE:-Debug}
QT_PREFIX=${QT_PREFIX:-"$HOME/Qt/6.10.3/macos"}

if command -v nproc >/dev/null 2>&1; then
    JOBS=${JOBS:-$(nproc)}
else
    JOBS=${JOBS:-8}
fi

DO_CONFIGURE=0
DO_BUILD=1
DO_TESTS=1
DO_FORMAT=1
DO_LINT=1

for arg in "$@"; do
    case "$arg" in
        --configure)  DO_CONFIGURE=1 ;;
        --no-tests)   DO_TESTS=0 ;;
        --no-format)  DO_FORMAT=0 ;;
        --no-lint)    DO_LINT=0 ;;
        --format-only) DO_CONFIGURE=0; DO_BUILD=0; DO_TESTS=0; DO_LINT=0 ;;
        -h|--help)    sed -n '2,/^$/p' "$0"; exit 0 ;;
        *)            echo "check.sh: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done

say() { printf '\n=== %s\n' "$*"; }

# ---- configure ------------------------------------------------------------
if [ "$DO_CONFIGURE" -eq 1 ] || { [ "$DO_BUILD" -eq 1 ] && [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; }; then
    say "Configuring $BUILD_DIR ($BUILD_TYPE, Qt: $QT_PREFIX)"
    if [ -d "$QT_PREFIX" ]; then
        cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
              -DCMAKE_PREFIX_PATH="$QT_PREFIX" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    else
        echo "check.sh: QT_PREFIX '$QT_PREFIX' does not exist; configuring with" \
             "Qt from the system (set QT_PREFIX to override)" >&2
        cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
              -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    fi
else
    say "Reusing configured build tree: $BUILD_DIR"
fi

# ---- build ----------------------------------------------------------------
if [ "$DO_BUILD" -eq 1 ]; then
    say "Building (jobs: $JOBS)"
    cmake --build "$BUILD_DIR" -j "$JOBS"
fi

# ---- tests ----------------------------------------------------------------
if [ "$DO_TESTS" -eq 1 ]; then
    say "Running tests"
    # Every test carries a TIMEOUT and a headless platform from
    # tests/CMakeLists.txt, so a hang fails the run instead of blocking.
    ctest --test-dir "$BUILD_DIR" -j "$JOBS" --output-on-failure
else
    say "Tests skipped"
fi

# ---- formatting -----------------------------------------------------------
if [ "$DO_FORMAT" -eq 1 ]; then
    say "Checking formatting"
    if command -v clang-format >/dev/null 2>&1; then
        # shellcheck disable=SC2046
        find "$ROOT/src" "$ROOT/tests" -name '*.cpp' -o -name '*.h' \
            | xargs clang-format --dry-run --Werror
        echo "clang-format: clean"
    else
        echo "clang-format not installed — SKIPPED (install LLVM to gate this)" >&2
    fi
fi

# ---- QML lint -------------------------------------------------------------
# qmllint only picks up .qmllint.ini from the analysed file's directory upwards,
# so it runs with resources/qml as the working directory. The config demotes the
# two categories that are noise or unreliable today (see .qmllint.ini); anything
# else — unresolved type, missing property, syntax error — fails the run.
if [ "$DO_LINT" -eq 1 ]; then
    say "Linting QML"
    # Prefer the Qt the project builds against: another qmllint on PATH (e.g. a
    # Homebrew Qt) has different .qmllint.ini keys and aborts with status 255.
    QMLLINT=""
    if [ -x "$QT_PREFIX/bin/qmllint" ]; then
        QMLLINT="$QT_PREFIX/bin/qmllint"
    else
        QMLLINT=$(command -v qmllint || true)
    fi
    if [ -z "$QMLLINT" ]; then
        echo "qmllint not found — SKIPPED" >&2
    elif [ ! -f "$BUILD_DIR/src/LLocr/qmldir" ]; then
        echo "the LLocr QML module is not built (no $BUILD_DIR/src/LLocr/qmldir) — SKIPPED" >&2
    else
        LIST=/tmp/llocr-qmllint-files.txt
        find "$ROOT/resources/qml" -name '*.qml' | sort > "$LIST"
        # Info-level output is ~700 lines of style notes; keep it for failures.
        if ! (cd "$ROOT" && xargs "$QMLLINT" --max-warnings 0 -I "build/src" < "$LIST") > /tmp/llocr-qmllint.log 2>&1; then
            cat /tmp/llocr-qmllint.log
            echo "qmllint reported problems (see above)" >&2
            exit 1
        fi
        echo "qmllint: clean"
    fi
fi

say "OK"
