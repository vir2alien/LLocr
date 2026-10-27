#!/bin/sh
# Give the macOS dev build a stable code identity, so the Keychain keeps the
# API key across rebuilds.
#
# Why: the Keychain binds an item to the code that created it, and the
# "identity" of an ad-hoc signed build is the hash of its code — a rebuild
# changes the hash, and the item becomes unreachable: the app finds no key
# after a restart, and the Keychain asks for a permission that a background
# read cannot answer. A build signed with a certificate has an identity that
# does not change between builds, so the item stays reachable.
#
# Usage:
#   scripts/macos-dev-sign.sh --create     create the identity (once)
#   scripts/macos-dev-sign.sh --sign FILE  sign the file, if the identity exists
#   scripts/macos-dev-sign.sh [FILE]       create the identity if needed, sign
#
# The identity is a self-signed certificate in a dedicated keychain next to the
# login one. It is a development convenience: it is not a Developer ID, it
# cannot be notarized, and it is not used for the release build.
#
# The certificate carries the code-signing extended key usage: without it
# macOS does not accept the certificate as a signing identity at all, and
# "security find-identity -p codesigning" lists nothing.

set -eu

IDENTITY="LLocr Dev"
KEYCHAIN_NAME="llocr-dev.keychain"
KEYCHAIN="$HOME/Library/Keychains/$KEYCHAIN_NAME"
LOGIN_KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"
KEYCHAIN_PASSWORD="llocr-dev"
WORK_DIR="${TMPDIR:-/tmp}/llocr-dev-sign.$$"

find_identity() {
    security find-identity -v -p codesigning "$KEYCHAIN" 2>/dev/null | grep -q "\"$IDENTITY\"" && return 0
    security find-identity -v -p codesigning 2>/dev/null | grep -q "\"$IDENTITY\"" && return 0
    return 1
}

# codesign looks for the signing identity in the keychain search list, and
# ignores its own --keychain option: a keychain outside the list is invisible
# to it ("no identity found"). The dev keychain joins the list next to the
# login one — a developer machine's setting, and it stays there.
add_to_search_list() {
    current=$(security list-keychains 2>/dev/null | sed 's/^[[:space:]]*//; s/"//g')
    case "
$current" in
    *"$KEYCHAIN-db"*) return 0 ;;
    esac
    security list-keychains -s $current "$KEYCHAIN-db" >/dev/null 2>&1 || true
}

create_identity() {
    if find_identity; then
        echo "The identity '$IDENTITY' already exists."
        # The identity may still be invisible to codesign: the keychain has
        # to be in the search list.
        add_to_search_list
        security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null 2>&1 || true
        return 0
    fi

    mkdir -p "$WORK_DIR"
    trap 'rm -rf "$WORK_DIR"' EXIT

    # A code-signing certificate: the extended key usage is what makes macOS
    # accept it as an identity, and "digitalSignature" is what it signs with.
    # Without the EKU the keychain stores the identity, and the code-signing
    # policy rejects it — "find-identity -p codesigning" then finds nothing.
    cat > "$WORK_DIR/llocr-dev.cnf" <<'CONFIG'
[req]
distinguished_name = dn
x509_extensions = v3_code_signing
prompt = no

[dn]
CN = LLocr Dev

[v3_code_signing]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature
extendedKeyUsage = codeSigning
CONFIG

    echo "Creating a self-signed identity '$IDENTITY' in $KEYCHAIN"
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$WORK_DIR/llocr-dev.key" \
        -out "$WORK_DIR/llocr-dev.crt" \
        -days 3650 \
        -config "$WORK_DIR/llocr-dev.cnf" >/dev/null 2>&1

    # "security create-keychain" writes the file with the "-db" suffix, and
    # refuses to create a second one with the same name.
    if [ ! -f "$KEYCHAIN" ] && [ ! -f "$KEYCHAIN-db" ]; then
        if ! security create-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"; then
            echo "The keychain could not be created: the system refused the write." >&2
            return 1
        fi
    fi
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"

    # An earlier attempt may have left an identity without the code-signing
    # usage: it is in the way, and the new certificate would not pair with
    # the old key.
    security delete-identity -c "$IDENTITY" "$KEYCHAIN" >/dev/null 2>&1 || true
    security delete-certificate -c "$IDENTITY" "$KEYCHAIN" >/dev/null 2>&1 || true

    # One bundle: the certificate and its private key travel together, so the
    # keychain gets a paired identity instead of two loose items.
    if ! openssl pkcs12 -export \
            -inkey "$WORK_DIR/llocr-dev.key" \
            -in "$WORK_DIR/llocr-dev.crt" \
            -out "$WORK_DIR/llocr-dev.p12" \
            -passout "pass:$KEYCHAIN_PASSWORD" \
            -name "$IDENTITY" >/dev/null 2>&1; then
        echo "The certificate bundle could not be built." >&2
        return 1
    fi
    if ! security import "$WORK_DIR/llocr-dev.p12" -k "$KEYCHAIN" -T /usr/bin/codesign -A -P "$KEYCHAIN_PASSWORD"; then
        if ! security import "$WORK_DIR/llocr-dev.crt" -k "$KEYCHAIN" -T /usr/bin/codesign -A; then
            echo "The certificate could not be imported into $KEYCHAIN." >&2
            return 1
        fi
    fi
    # A self-signed certificate is unknown to the system: the user's trust
    # settings have to accept it as a root, or it is not a valid identity.
    security add-trusted-cert -r trustRoot -k "$LOGIN_KEYCHAIN" "$WORK_DIR/llocr-dev.crt" >/dev/null 2>&1 || true
    # A key in a non-default keychain needs its partition list, or codesign
    # refuses to use it without a GUI confirmation.
    security set-key-partition-list -S apple-tool:,apple: -s -k "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null 2>&1 || true
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN"
    add_to_search_list

    if find_identity; then
        echo "The identity '$IDENTITY' is ready."
        return 0
    fi

    echo "The identity was not created. What the system says about the keychain:" >&2
    security find-identity -v -p codesigning "$KEYCHAIN" >&2 || true
    security list-keychains >&2 || true
    return 1
}

sign_binary() {
    binary="$1"
    if [ ! -f "$binary" ]; then
        echo "No such file: $binary" >&2
        return 1
    fi
    if ! find_identity; then
        echo "The identity '$IDENTITY' is missing: $binary stays ad-hoc signed." >&2
        echo "Run 'scripts/macos-dev-sign.sh --create' once to create it." >&2
        return 1
    fi
    # The keychain is locked again after a while, and codesign would ask the
    # user instead of signing.
    security unlock-keychain -p "$KEYCHAIN_PASSWORD" "$KEYCHAIN" >/dev/null 2>&1 || true
    add_to_search_list
    codesign --force --sign "$IDENTITY" --keychain "$KEYCHAIN" "$binary" 2>&1 | sed 's/^/  codesign: /'

    # codesign reports a failure and still exits with 0, so the result is
    # checked: an ad-hoc binary keeps the code hash as its identity, and that
    # hash changes with every rebuild — the key would be lost again.
    requirement=$(codesign -d -r- "$binary" 2>/dev/null | tail -1)
    case "$requirement" in
    *cdhash*)
        echo "The binary is still ad-hoc signed ($requirement): the identity was not used." >&2
        return 1
        ;;
    esac
    echo "Signed $binary with '$IDENTITY'"
    echo "  requirement: $requirement"
}

case "${1:---create}" in
--create)
    create_identity
    ;;
--sign)
    shift
    # Best effort: the build must not fail because the identity is missing.
    # The reason is printed, so it is not a silent skip.
    sign_binary "${1:-}" || true
    ;;
*)
    binary="$1"
    if [ -z "$binary" ]; then
        binary="$(cd "$(dirname "$0")/.." && pwd)/build/bin/llocr.real"
    fi
    create_identity || true
    sign_binary "$binary" || true
    ;;
esac
