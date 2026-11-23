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

The committed `draw_copy.run.exe` is the exact MinGW artifact from run 149 at c2aaad5:
https://github.com/AKgame47/Anyxbox/actions/runs/45232791579/job/137896509288
SHA-256: `3ca114889b4e22f7a47bf20c2180b9676569fb7b54d9eae38f833ef662f6be69` (15610 bytes).
The directory-mode draw-copy test now runs both the source-call path and this retained binary.
CI also explicitly verifies the retained binary with required Vulkan, separate from the freshly built binary.
`test_fixture_slot` requires zero exit, empty output/error, two presents and four shader translations.

## draw_region.c, draw_update.c, draw_map.c
These select methods 1, 2 and 3 of the shared scene draw-copy fixture.
Region-copy and UpdateSubresource replace only the right texture column and top 20 backbuffer rows.
Their verifier also checks the left column and lower background stay unchanged.
Map/Unmap writes a dynamic sampled texture directly. For the backbuffer it writes a staging texture,
then CopyResource transfers staging bytes to the live target, rather than mapping a default render target.
All use two presents and require Vulkan in CI's independently compiled binary checks. Fresh executables
and both frames are retained. Independent compilation/execution of these new variants awaits CI.

The three retained `.run.exe` variants are exact artifacts from run 159 at source commit 92d1420:
https://github.com/AKgame47/Anyxbox/actions/runs/45239239109/job/137919083466
- draw_region.run.exe: 15610 bytes, SHA-256 `165bbf157652c550144c8b5b2180979163e64c7cf9e5524b32e2b5ffa5d22442`.
- draw_update.run.exe: 15610 bytes, SHA-256 `395f86ab0ab09bbf08f083a2045a3807b462a0f83546fc6dabf926f44c3fa819`.
- draw_map.run.exe: 15062 bytes, SHA-256 `2f4782134501d53a7c3b1138a5a3aabbbd48d77d9776cb54a780ac7eeabbdf1a`.
Directory-mode tests cover source calls and retained variants. CI also explicitly requires Vulkan
for each freshly built and retained variant and keeps all captured frames. No physical-GPU coverage is implied.

## asset_read.c
Self-written freestanding MinGW fixture for explicit read-only launcher asset access.
Build with the rooted-asset step in fixture.yml. Create `data/message.txt` under a temporary test root
with the exact text `Anyxbox rooted asset` and a newline. Run `run_exe --i-built-this asset_read.exe --asset-root ROOT`.
The program reads/verifies the text, rejects an attempted truncating write and parent traversal, prints the text and exits zero.
Without an asset root it must exit 10. Independent compilation and execution await CI; not yet a retained binary.

`asset_read.run.exe` is the exact run-173 MinGW artifact at source commit 0bc862a:
https://github.com/AKgame47/Anyxbox/actions/runs/45243811043/job/137935175917
7617 bytes; SHA-256 `f5e5e87a83bad67ec17d4e0bb08989fdea1761d7f4840a42789219680922c10a`.
The fixture-slot test creates its own temporary asset root, checks exact guest output and unchanged asset contents,
and separately checks default denial. CI repeats actual-CLI checks for fresh and retained binaries.

## asset_scene.c
Self-written scene with a 512x512 raw RGBA texture read from `data/texture.rgba` through the explicit read-only root.
The fixture uses scene.c's authored shader/geometry and presents twice. The test generates a 1 MiB four-quadrant asset
in a temporary directory, passes its root to run_pe and checks both frames on Vulkan. Host mode binds the same rooted
file functions. CI compiles the independent MinGW binary and requires Vulkan for its pixel checks; compile/run awaits CI.

The retained `asset_scene.run.exe` is the exact MinGW artifact from run 187, source commit 1d9405f:
https://github.com/AKgame47/Anyxbox/actions/runs/45248161131/job/137950337383
15111 bytes; SHA-256 `72ee6ddf07f1ef8963c4976f03bf331e7544aaf5f519d9620c1d6528488c3008`.
The directory-mode scene verifier runs source calls and this retained binary. CI requires Vulkan separately
for fresh and retained executables and retains both frame pairs. Fixture-slot checks default-denial exit40,
then executes with a generated asset root; dedicated Vulkan tests own its pixel assertions.

## keyboard_scene.c
File-backed 512 texture scene with arrow-key movement of the green quad and Escape to stop.
Default four frames for CI. Build with `-DFRAMES=3600` for a bounded interactive run, then use
`run_exe --i-built-this keyboard_scene.exe --window --gpu --asset-root ROOT` with the generated quadrant asset.
`test_fixture_keyboard_scene` supplies scripted host events after each present: right-down, right-up,
then right-down followed by focus-loss. It verifies movement and identical stopped frames after release/focus-loss.
Scripted event tests are not physical-keyboard evidence. Independent MinGW compilation/run awaits CI.
