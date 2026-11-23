Drop self-built Windows x64 test programs here (`*.exe`), together with their source and build recipe in the same
folder. `test_fixture_slot` loads each one, prints which imports Anyxbox does not cover yet, and runs it when its
name ends in `.run.exe`. Homebrew and self-built binaries only (see docs/SCOPE.md).

## triangle.c
`triangle.c` is our own freestanding D3D11 triangle (window class, window, swap chain, vertex buffer, precompiled DXBC, three frames).
No C runtime and no Microsoft headers: the few types it needs are declared in the file. `.github/workflows/fixture.yml` builds it with
Ubuntu's MinGW-w64 package and runs `import_gap` and `run_exe` on the result; read the job log on GitHub. The CI job is read-only
(`permissions: contents: read`), pushes nothing and fetches no third-party binaries. To use the built exe as a test, download the
`fixture-exes` artifact and commit it as `tests/fixtures/triangle.run.exe`.
`triangle_shaders.h` is generated: `gen_fixture_shaders > tests/fixtures/triangle_shaders.h`.
`test_fixture_source` compiles the same triangle.c as a host function (imports bound to Anyxbox functions), so its call sequence is tested
here without a Windows toolchain. It does not prove the MinGW build.

## texture_copy.cpp
The self-written texture-copy fixture uses MinGW D3D11 headers and default C++ startup.
Build: `x86_64-w64-mingw32-g++ -std=c++17 -O1 -static -Wall -Wextra -o texture_copy.exe tests/fixtures/texture_copy.cpp -ld3d11`.
`texture_copy.run.exe` is the exact CI-built artifact from run 141 at source commit 8f00833:
https://github.com/AKgame47/Anyxbox/actions/runs/45226825014/job/137875448739
SHA-256: `a88610e4658caec2e7b9c0e48f512e856d7812a8f2bf226983a2559fc22fc6d8` (502094 bytes).
It checks whole mip-chain copies, region copies, staging Map readback and generated-mip copying.
`test_fixture_slot` requires its four exact output lines, zero exit, no runtime error and zero presented frames/shader translations.
This fixture tests resource operations only, not drawing or GPU rendering.

## draw_copy.c
This source selects the draw-copy variant of scene.c and reuses its authored DXBC and geometry.
CI builds it with the same freestanding MinGW command as scene.c (substitute draw_copy for scene).
After frame 1 it copies yellow into the sampled texture and cyan into the active backbuffer.
Frame 2 redraws without clearing either color or depth, using LESS_EQUAL depth testing.
`test_fixture_draw_copy draw_copy.exe --require-vulkan draw-copy-frame` runs the independent binary,
requires Vulkan, checks pixels from both presents and retains both frames as PPM images.
With a fixture-directory argument instead of an executable, the test runs the same source as host calls.
That local host-source test is not proof of the MinGW binary; its compile and execution await CI.
