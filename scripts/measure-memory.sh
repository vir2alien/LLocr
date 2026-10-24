#!/usr/bin/env bash
# Probe llama.cpp for the memory numbers of a model: the KV cache, the working
# set, the vision encoder and the draft's own context.
#
# The numbers it prints are the fields of the "memory" block of a model
# profile (resources/profiles/models/<id>.json). ADR 191 explains what the
# estimate is; docs/27-memory-estimate.md explains how to measure a new model
# and what the traps are.
#
# Usage:
#   scripts/measure-memory.sh --model <repo/file.gguf> [options]
#   scripts/measure-memory.sh --parse <log> [--log <log> ...]
#
# Options:
#   --model FILE          the model, relative to the models directory
#   --mmproj FILE         the vision projector, relative to the models directory
#   --draft FILE          the speculative decoding draft, relative to the models directory
#   --ctx N               the context window (the app's macOS profile: 16384)
#   --ubatch N            the ubatch (llama.cpp's default: 512)
#   --batch N             the batch (llama.cpp's default: 2048)
#   --flash-attn on|off   flash attention (the app's platform profile decides)
#   --image-min-tokens N  the floor of the image budget
#   --image-max-tokens N  the ceiling of the image budget
#   --spec-type TYPE      draft-mtp or draft-dspark
#   --spec-draft-n-max N  how many tokens the draft may propose
#   --special             the --special flag (the models trained on it)
#   --no-warmup           skip the warmup (the app's policy flag). The warmup is
#                         on by default: the clip graph is reserved for the
#                         dummy image of the warmup, and that reservation is
#                         where the vision numbers come from.
#   --runtime DIR         the llama.cpp build directory
#   --models DIR          the models directory
#   --out DIR             where the log goes (default: build/measure-memory)
#   --port N              the port (default: 18700 + a free offset)
#   --boot SECONDS        how long the server may load (default: 26)
#   --parse LOG           parse an existing log instead of running the server
#
# The server runs on the CPU (--device none --n-gpu-layers 0): the buffer
# sizes llama.cpp reserves are the same, and the numbers are then readable
# without a card.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
RUNTIME="${LLOCR_RUNTIME:-}"
MODELS="${LLOCR_MODELS:-}"
OUT="${LLOCR_MEASURE_OUT:-build/measure-memory}"
PORT=""
BOOT=26
CTX=""
UBATCH=""
BATCH=""
FLASH=""
MIN_TOKENS=""
MAX_TOKENS=""
SPEC_TYPE=""
SPEC_N_MAX=""
SPECIAL=""
NO_WARMUP=""
MODEL=""
MMPROJ=""
DRAFT=""
PARSE_LOGS=()

usage() {
    sed -n '2,/^set -eu$/p' "$0" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --model) MODEL="$2"; shift 2 ;;
        --mmproj) MMPROJ="$2"; shift 2 ;;
        --draft) DRAFT="$2"; shift 2 ;;
        --ctx) CTX="$2"; shift 2 ;;
        --ubatch) UBACK="$2"; shift 2 ;;
        --batch) BATCH="$2"; shift 2 ;;
        --flash-attn) FLASH="$2"; shift 2 ;;
        --image-min-tokens) MIN_TOKENS="$2"; shift 2 ;;
        --image-max-tokens) MAX_TOKENS="$2"; shift 2 ;;
        --spec-type) SPEC_TYPE="$2"; shift 2 ;;
        --spec-draft-n-max) SPEC_N_MAX="$2"; shift 2 ;;
        --special) SPECIAL="--special"; shift ;;
        --no-warmup) NO_WARMUP="--no-warmup"; shift ;;
        --runtime) RUNTIME="$2"; shift 2 ;;
        --models) MODELS="$2"; shift 2 ;;
        --out) OUT="$2"; shift 2 ;;
        --port) PORT="$2"; shift 2 ;;
        --boot) BOOT="$2"; shift 2 ;;
        --parse) PARSE_LOGS+=("$2"); shift 2 ;;
        --log) PARSE_LOGS+=("$2"); shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [ ${#PARSE_LOGS[@]} -gt 0 ]; then
    exec python3 "$HERE/memory-from-log.py" "${PARSE_LOGS[@]}"
fi

if [ -z "$MODEL" ]; then
    echo "--model is required (or --parse for an existing log)" >&2
    exit 2
fi

if [ -z "$MODELS" ]; then
    case "$(uname -s)" in
        Darwin) MODELS="$HOME/Library/Application Support/llocr/LLM OCR/models" ;;
        Linux) MODELS="$HOME/.local/share/llocr/LLM OCR/models" ;;
        *) echo "set LLOCR_MODELS to the models directory" >&2; exit 2 ;;
    esac
fi

if [ -z "$RUNTIME" ]; then
    case "$(uname -s)" in
        Darwin) RUNTIME_ROOT="$HOME/Library/Application Support/llocr/LLM OCR/runtime" ;;
        Linux) RUNTIME_ROOT="$HOME/.local/share/llocr/LLM OCR/runtime" ;;
        *) echo "set LLOCR_RUNTIME to the llama.cpp build directory" >&2; exit 2 ;;
    esac
    RUNTIME=$(ls -dt "$RUNTIME_ROOT"/llama.cpp-*/ 2>/dev/null | head -1)
    RUNTIME="${RUNTIME%/}"
    if [ ! -x "$RUNTIME/llama-server" ]; then
        RUNTIME=$(dirname "$(find "$RUNTIME" -maxdepth 2 -name llama-server -type f 2>/dev/null | head -1)")
    fi
fi

SERVER="$RUNTIME/llama-server"
if [ ! -x "$SERVER" ]; then
    echo "no llama-server in $RUNTIME" >&2
    exit 2
fi

mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
if [ -z "$PORT" ]; then
    PORT=$((18700 + RANDOM % 900))
fi
LOG="$OUT/$(basename "$MODEL" .gguf)-$PORT.log"

args=(
    --model "$MODELS/$MODEL"
    --alias llocr-probe
    --host 127.0.0.1
    --port "$PORT"
    --device none
    --n-gpu-layers 0
    --parallel 1
    --cache-reuse 0
    --no-context-shift
    -lv 5
)
[ -n "$MMPROJ" ] && args+=(--mmproj "$MODELS/$MMPROJ")
[ -n "$DRAFT" ] && args+=(--model-draft "$MODELS/$DRAFT")
[ -n "$CTX" ] && args+=(--ctx-size "$CTX")
[ -n "$UBATCH" ] && args+=(--ubatch-size "$UBATCH")
[ -n "$BATCH" ] && args+=(--batch-size "$BATCH")
[ -n "$FLASH" ] && args+=(--flash-attn "$FLASH")
[ -n "$MIN_TOKENS" ] && args+=(--image-min-tokens "$MIN_TOKENS")
[ -n "$MAX_TOKENS" ] && args+=(--image-max-tokens "$MAX_TOKENS")
[ -n "$SPEC_TYPE" ] && args+=(--spec-type "$SPEC_TYPE")
[ -n "$SPEC_N_MAX" ] && args+=(--spec-draft-n-max "$SPEC_N_MAX")
[ -n "$SPECIAL" ] && args+=("$SPECIAL")
[ -n "$NO_WARMUP" ] && args+=("$NO_WARMUP")

echo "== $SERVER"
echo "   log $LOG"
cd "$RUNTIME"
"$SERVER" "${args[@]}" > "$LOG" 2>&1 &
server_pid=$!
sleep "$BOOT"
kill "$server_pid" 2>/dev/null || true
wait "$server_pid" 2>/dev/null || true

python3 "$HERE/memory-from-log.py" "$LOG"
