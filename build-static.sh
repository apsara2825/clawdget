#!/bin/sh
# build-static.sh - build a fully static clawdget binary for any cross toolchain.
#
# Downloads and statically builds mbedtls + curl for the target, then links
# clawdget against them with -static. Result: a single-file binary with zero
# runtime dependencies.
#
# Usage:
#   CROSS=mipsel-linux-musl- ./scripts/build-static.sh [output-dir]
#   CROSS=arm-linux-gnueabi- ./scripts/build-static.sh build-arm
#
# Env:
#   CROSS    toolchain prefix, e.g. "mipsel-linux-musl-" (required)
#   JOBS     parallel make jobs (default: nproc)
#   OUT      output dir (default: build-static)

set -e
cd "$(dirname "$0")"

: "${CROSS:?set CROSS=<toolchain-prefix>, e.g. CROSS=mipsel-linux-musl-}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
OUT="${1:-build-static}"
DEPS=".build-deps"
MBEDTLS_VER=2.28.8
CURL_VER=7.88.1
CC="${CROSS}gcc"
AR="${CROSS}ar"
TRIPLE="$(basename "${CROSS%-}")"

mkdir -p "$DEPS" "$OUT"

DEPS_ABS="$(mkdir -p "$DEPS" && cd "$DEPS" && pwd)"
echo "==> fetching sources"
if [ ! -d "$DEPS/mbedtls-$MBEDTLS_VER" ]; then
    [ -f "$DEPS/mbedtls-$MBEDTLS_VER.tar.gz" ] || \
        curl -sL --retry 3 -o "$DEPS/mbedtls-$MBEDTLS_VER.tar.gz" \
        "https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v$MBEDTLS_VER.tar.gz"
    tar xzf "$DEPS/mbedtls-$MBEDTLS_VER.tar.gz" -C "$DEPS"
fi
if [ ! -d "$DEPS/curl-$CURL_VER" ]; then
    [ -f "$DEPS/curl-$CURL_VER.tar.gz" ] || \
        curl -sL --retry 3 -o "$DEPS/curl-$CURL_VER.tar.gz" \
        "https://curl.se/download/curl-$CURL_VER.tar.gz"
    tar xzf "$DEPS/curl-$CURL_VER.tar.gz" -C "$DEPS"
fi

echo "==> building mbedtls"
STAGE="$DEPS_ABS/stage-$TRIPLE"
rm -rf "$STAGE"
mkdir -p "$STAGE/include" "$STAGE/lib"
if [ ! -f "$DEPS/mbedtls-$MBEDTLS_VER/library/libmbedcrypto.a" ] || \
   [ "$DEPS/mbedtls-$MBEDTLS_VER/library/libmbedcrypto.a" -ot "$DEPS/mbedtls-$MBEDTLS_VER.tar.gz" ]; then
    # SHARED=1 so curl's configure can detect mbedtls via the .so files;
    # the final clawdget link still uses the .a archives (fully static).
    make -C "$DEPS/mbedtls-$MBEDTLS_VER" clean > /dev/null 2>&1 || true
    make -C "$DEPS/mbedtls-$MBEDTLS_VER" -j"$JOBS" SHARED=1 \
        CC="$CC" AR="$AR" CFLAGS="-std=gnu99 -O2" > /dev/null
fi
cp -r "$DEPS/mbedtls-$MBEDTLS_VER/include/." "$STAGE/include/"
cp "$DEPS/mbedtls-$MBEDTLS_VER/library/"*.a "$STAGE/lib/" 2>/dev/null || true
cp "$DEPS/mbedtls-$MBEDTLS_VER/library/"libmbed*.so* "$STAGE/lib/" 2>/dev/null || true

echo "==> building curl (static, mbedtls backend)"
if [ ! -f "$DEPS/curl-$CURL_VER/lib/.libs/libcurl.a" ]; then
    (
    cd "$DEPS/curl-$CURL_VER"
    make clean > /dev/null 2>&1 || true
    ./configure \
        --build="$(cc -dumpmachine 2>/dev/null || echo x86_64-pc-linux-gnu)" \
        --host="$TRIPLE" \
        --with-mbedtls="$STAGE" \
        --without-openssl --without-zlib --without-brotli --without-zstd \
        --without-libpsl --without-libidn2 --without-nghttp2 \
        --without-librtmp --without-libssh2 \
        --disable-ldap --disable-ldaps --disable-manual \
        --disable-threaded-resolver \
        --disable-shared --enable-static \
        CC="$CC" LDFLAGS="-Wl,-rpath-link=$STAGE/lib" \
        > "$DEPS_ABS/curl-conf.log" 2>&1
    make -j"$JOBS" > "$DEPS_ABS/curl-build.log" 2>&1
    )
[ -f "$DEPS/curl-$CURL_VER/lib/.libs/libcurl.a" ] || \
    { echo "ERROR: curl build failed, see $DEPS_ABS/curl-conf.log / curl-build.log"; exit 1; }
fi

echo "==> linking clawdget (static)"
STATIC_LIBS="-lpthread -ldl"
if ! $CC -std=gnu99 -Os -Wall -Isrc -Ithirdparty \
        -I"$DEPS/curl-$CURL_VER/include" -I"$STAGE/include" \
        src/*.c thirdparty/cjson.c \
        "$DEPS/curl-$CURL_VER/lib/.libs/libcurl.a" \
        "$STAGE/lib/libmbedtls.a" "$STAGE/lib/libmbedx509.a" \
        "$STAGE/lib/libmbedcrypto.a" \
        -static $STATIC_LIBS -o "$OUT/clawdget" 2>/dev/null; then
    # some toolchains (e.g. ARM) need the unwind helpers explicitly
    $CC -std=gnu99 -Os -Wall -Isrc -Ithirdparty \
        -I"$DEPS/curl-$CURL_VER/include" -I"$STAGE/include" \
        src/*.c thirdparty/cjson.c \
        "$DEPS/curl-$CURL_VER/lib/.libs/libcurl.a" \
        "$STAGE/lib/libmbedtls.a" "$STAGE/lib/libmbedx509.a" \
        "$STAGE/lib/libmbedcrypto.a" \
        -static $STATIC_LIBS -lgcc_eh -o "$OUT/clawdget"
fi
${CROSS}strip "$OUT/clawdget" 2>/dev/null || true

echo "==> done: $OUT/clawdget"
file "$OUT/clawdget" 2>/dev/null || true
