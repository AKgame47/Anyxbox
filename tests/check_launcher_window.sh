#!/bin/sh
# Run under Xvfb. Open results only, not window screenshot/presentation parity.
set -eu
runner=$1
expect_failure(){
  log=$1;shift
  set +e
  "$@" > "$log" 2>&1
  status=$?
  set -e
  test "$status" = 1 || { cat "$log"; echo "expected exit1 got $status";exit 1; }
}
expect_failure launcher-window-denied.log env SDL_VIDEODRIVER=anyxbox-no-such-driver "$runner" --i-built-this tests/fixtures/triangle.run.exe --window
grep -q 'window: attempts=1 opened=0 failed=1 request=failed' launcher-window-denied.log
expect_failure launcher-window-absent.log "$runner" --i-built-this tests/fixtures/hello_crt.run.exe --window
grep -q 'window: attempts=0 opened=0 failed=0 request=failed' launcher-window-absent.log
"$runner" --i-built-this tests/fixtures/triangle.run.exe --window --gpu > launcher-window-success.log 2>&1
grep -q 'loaded=1 ran=1 exit=0' launcher-window-success.log
grep -q 'window: attempts=1 opened=1 failed=0 request=satisfied' launcher-window-success.log
echo 'launcher window: unavailable display and absent guest window fail; real SDL open under Xvfb succeeds'
