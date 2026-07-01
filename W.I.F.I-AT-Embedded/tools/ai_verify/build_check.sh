#!/usr/bin/env bash
# 보드 없는 검증 #1 — 타깃 컴파일 검증.
#
#   (A) ESP32(xtensa) gcc 로 gru_functions.c 를 컴파일 → 실기기 빌드 성립 확인
#       (문법/타입/배열크기/암시적선언 오류를 실행 없이 잡는다)
#   (B) 네이티브 gcc/clang 이 있으면 test_gru_host.c 를 빌드+실행 → 수치 검증
#
# 사용: bash build_check.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
AIDIR="$HERE/../../components/AI"
rc=0

echo "== (A) ESP32(xtensa) 타깃 컴파일: gru_functions.c =="
XGCC=$(ls /c/Espressif/tools/xtensa-esp-elf/*/xtensa-esp-elf/bin/xtensa-esp32-elf-gcc.exe 2>/dev/null | head -1)
if [ -z "$XGCC" ]; then XGCC=$(command -v xtensa-esp32-elf-gcc); fi
if [ -n "$XGCC" ]; then
  "$XGCC" -std=gnu11 -Os -Wall -Wextra -mlongcalls -c "$AIDIR/gru_functions.c" \
      -I "$AIDIR" -o "$HERE/gru_functions.obj"
  if [ $? -eq 0 ]; then
    sz=$("$(dirname "$XGCC")/xtensa-esp32-elf-size.exe" "$HERE/gru_functions.obj" 2>/dev/null | tail -1)
    echo "  PASS — 컴파일 성공. size(text data bss dec): $sz"
    rm -f "$HERE/gru_functions.obj"
  else
    echo "  FAIL — 타깃 컴파일 오류"; rc=1
  fi
else
  echo "  SKIP — xtensa gcc 미발견 (ESP-IDF 환경에서 재시도)"
fi

echo "== (B) 네이티브 실행 테스트: test_gru_host.c =="
NGCC=$(command -v gcc || command -v clang || command -v cc)
if [ -n "$NGCC" ]; then
  "$NGCC" -std=c11 -O2 "$HERE/test_gru_host.c" -I "$AIDIR" -lm -o "$HERE/test_gru_host.exe" \
    && "$HERE/test_gru_host.exe"; trc=$?
  rm -f "$HERE/test_gru_host.exe"
  [ $trc -ne 0 ] && rc=1
else
  echo "  SKIP — 네이티브 gcc/clang 미발견. 수치 검증은 gru_reference.py 로 대체 실행."
fi

echo "==== build_check 종료 (rc=$rc) ===="
exit $rc
