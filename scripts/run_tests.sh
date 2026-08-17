#!/usr/bin/env sh
set -eu

MODE="debug"
FORCE_RECONFIGURE=0
VERBOSE=0
UNIT_ONLY=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    debug|release|both)
      MODE="$1"
      ;;
    --reconfigure)
      FORCE_RECONFIGURE=1
      ;;
    --verbose)
      VERBOSE=1
      ;;
    --unit)
      UNIT_ONLY=1
      ;;
    *)
      echo "Usage: $(basename "$0") [debug|release|both] [--reconfigure] [--verbose] [--unit]" >&2
      exit 2
      ;;
  esac
  shift
done

if [ "$UNIT_ONLY" -eq 1 ] && [ "$MODE" = "both" ]; then
  echo "--unit supports one configuration at a time; choose debug or release" >&2
  exit 2
fi

ROOT_DIR=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT_DIR"

BUILD_JOBS=${DRAXUL_BUILD_JOBS:-}
if [ -z "$BUILD_JOBS" ]; then
  BUILD_JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
fi
if [ -z "$BUILD_JOBS" ]; then
  BUILD_JOBS=8
fi
case "$BUILD_JOBS" in
  *[!0-9]*|0)
    echo "DRAXUL_BUILD_JOBS must be a positive integer (got '$BUILD_JOBS')" >&2
    exit 2
    ;;
esac

run() {
  echo
  echo "> $*"
  "$@"
}

cache_build_type() {
  if [ ! -f build/CMakeCache.txt ]; then
    return 1
  fi
  sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' build/CMakeCache.txt | head -n 1
}

cache_value() {
  key="$1"
  if [ ! -f build/CMakeCache.txt ]; then
    return 1
  fi
  sed -n "s/^${key}:[^=]*=//p" build/CMakeCache.txt | head -n 1
}

should_configure() {
  config="$1"

  if [ "$FORCE_RECONFIGURE" -eq 1 ]; then
    return 0
  fi

  if [ ! -f build/CMakeCache.txt ]; then
    return 0
  fi

  if [ ! -f build/Makefile ]; then
    return 0
  fi

  cached_type=$(cache_build_type || true)
  if [ -z "$cached_type" ]; then
    return 0
  fi

  if [ "$cached_type" != "$config" ]; then
    return 0
  fi

  for option in DRAXUL_ENABLE_SANITIZERS DRAXUL_ENABLE_TSAN DRAXUL_ENABLE_COVERAGE; do
    if [ "$(cache_value "$option" || true)" = "ON" ]; then
      return 0
    fi
  done

  [ "$(cache_value DRAXUL_ENABLE_RENDER_TESTS || true)" != "ON" ]
}

run_config() {
  config="$1"
  case "$config" in
    Debug)
      preset="mac-debug"
      ;;
    Release)
      preset="mac-release"
      ;;
    *)
      echo "Unsupported config: $config" >&2
      exit 2
      ;;
  esac

  echo
  echo "=== $config ==="
  if should_configure "$config"; then
    run cmake --preset "$preset"
  else
    echo
    echo "> using existing CMake cache: build/CMakeCache.txt"
  fi
  if [ "$UNIT_ONLY" -eq 1 ]; then
    run cmake --build build --target draxul-tests --parallel "$BUILD_JOBS"
    if [ "$VERBOSE" -eq 1 ]; then
      run ctest --test-dir build --label-regex unit --parallel 4 --verbose --timeout 120
    else
      run ctest --test-dir build --label-regex unit --parallel 4 --output-on-failure --timeout 120
    fi
    return
  fi

  run cmake --build build --parallel "$BUILD_JOBS"
  if [ "$VERBOSE" -eq 1 ]; then
    run ctest --test-dir build --parallel 4 --verbose --timeout 120
  else
    run ctest --test-dir build --parallel 4 --progress --output-on-failure --timeout 120
  fi
}

case "$MODE" in
  debug)
    run_config Debug
    ;;
  release)
    run_config Release
    ;;
  both)
    run_config Debug
    run_config Release
    ;;
esac

echo
echo "All requested tests passed."
