#!/bin/sh
# Run from the source root, with the freshly built launch tool as the only argument.
set -eu
runner=$1
expect_usage() {
  set +e
  "$runner" "$@" > launcher-cli-rejection.tmp 2>&1
  status=$?
  set -e
  test "$status" = 2 || { cat launcher-cli-rejection.tmp; echo "expected usage exit2, got $status"; exit 1; }
  grep -q '^usage: run_exe ' launcher-cli-rejection.tmp
  if grep -q 'loaded=' launcher-cli-rejection.tmp; then cat launcher-cli-rejection.tmp; exit 1; fi
}
expect_usage --i-built-this tests/fixtures/hello_crt.run.exe --gup
expect_usage --i-built-this tests/fixtures/hello_crt.run.exe tests/fixtures/tls_crt.run.exe
expect_usage --i-built-this tests/fixtures/hello_crt.run.exe --asset-root
expect_usage --i-built-this tests/fixtures/hello_crt.run.exe --save-frame --gpu
expect_usage --i-built-this tests/fixtures/hello_crt.run.exe --asset-root a --asset-root b
expect_usage tests/fixtures/hello_crt.run.exe
expect_usage --i-built-this --
"$runner" --i-built-this -- tests/fixtures/hello_crt.run.exe > launcher-cli-success.log 2>&1
grep -q 'loaded=1 ran=1 exit=0' launcher-cli-success.log
grep -q 'hello from a MinGW program, argc=1' launcher-cli-success.log
rm launcher-cli-rejection.tmp
echo 'launcher CLI: seven invalid invocations rejected before launch, explicit path success'
