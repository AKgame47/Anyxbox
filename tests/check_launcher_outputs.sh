#!/bin/sh
# Verify explicit GPU/frame requirements through the actual launch tool.
set -eu
runner=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
expect_failure() {
  log=$1; shift
  set +e
  "$@" > "$log" 2>&1
  status=$?
  set -e
  test "$status" = 1 || { cat "$log"; echo "expected exit1, got $status"; exit 1; }
}
expect_failure launcher-gpu-denied.log env VK_ICD_FILENAMES="$work/no-driver.json" VK_DRIVER_FILES="$work/no-driver.json" "$runner" --i-built-this tests/fixtures/hello_crt.run.exe --gpu
grep -q 'requested Vulkan backend unavailable' launcher-gpu-denied.log
if grep -q 'loaded=' launcher-gpu-denied.log; then cat launcher-gpu-denied.log; exit 1; fi
expect_failure launcher-frame-absent.log "$runner" --i-built-this tests/fixtures/hello_crt.run.exe --save-frame "$work/absent.png"
grep -q 'loaded=1 ran=1 exit=0' launcher-frame-absent.log
grep -q 'failed (no valid frame or output not writable)' launcher-frame-absent.log
test ! -e "$work/absent.png"
expect_failure launcher-frame-denied.log "$runner" --i-built-this tests/fixtures/triangle.run.exe --save-frame "$work/missing/output.png"
grep -q 'loaded=1 ran=1 exit=0' launcher-frame-denied.log
grep -q 'failed (no valid frame or output not writable)' launcher-frame-denied.log
"$runner" --i-built-this tests/fixtures/triangle.run.exe --gpu --save-frame launcher-required-gpu.png > launcher-gpu-success.log 2>&1
grep -q 'gpu: Vulkan backend active' launcher-gpu-success.log
grep -q 'loaded=1 ran=1 exit=0' launcher-gpu-success.log
grep -q 'saved frame launcher-required-gpu.png: ok' launcher-gpu-success.log
test -s launcher-required-gpu.png
echo 'launcher outputs: explicit GPU refused without driver; absent/unwritable frame returns failure; Vulkan PNG succeeds'
