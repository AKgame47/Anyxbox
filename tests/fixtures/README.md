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

Retained `keyboard_scene.run.exe` is the exact run-202 artifact, source commit 5c1a2e7:
https://github.com/AKgame47/Anyxbox/actions/runs/45253435150/job/137968855839
15999 bytes; SHA-256 `96dde17e0c7abf209c660568bc59ecc0a460a7e0a66787f09f5d18343a1d3471`.
Directory-mode keyboard verifier checks source calls and this retained PE with scripted host input.
CI requires Vulkan for fresh and retained paths and retains all four frames from each.
Fixture-slot checks default root denial and rooted execution, not movement pixels.

`test_fixture_keyboard_sdl` uses the same source/retained executable and pixel verifier, but wraps the real SDL
window rather than ScriptHost. It injects key-down/up/focus-loss with SDL_PushEvent and uses the production
SDL_PollEvent translation and user32 held-state code. All four frames are presented to the SDL window as well
as captured for pixel assertions. Between frames the test clears unrelated Xvfb events before injecting its
known sequence. This is synthetic SDL queue coverage, not a physical keyboard or uncontrolled focus test.

## resource_interfaces.cpp
Public d3d11.h-based C++ probe, built separately by MinGW in CI. Checks device/context
and buffer/color/depth resource interface identity, inherited interfaces, GetType,
GetDevice and an unsupported-interface query. Not a rendered scene. Build:
`x86_64-w64-mingw32-g++ -std=c++17 -O1 -static -Wall -Wextra -o resource_interfaces.exe tests/fixtures/resource_interfaces.cpp -ld3d11`.
CI requires zero exit and its completion marker, and retains the executable and log.
Independent binary evidence is pending CI; no retained binary is supplied in batch 72.

Retained `resource_interfaces.run.exe` is the exact run-212 artifact, source tip 622ff44:
https://github.com/AKgame47/Anyxbox/actions/runs/45258594810/job/137986902854
503122 bytes; SHA-256 `5a28ec84f3674fbe601cd7851d1c30afa1a8c3e04ada9acba286a0120b1210f1`.
Its independent public-header build and strict execution passed, covering 51 imports.
Fixture-slot now requires its exact output, zero exit, zero frames/shaders and empty
D3D11 error. Fresh and retained runs remain separate strict CI steps. This probe
checks interface operations, not lifetime completeness or graphics output.

Batch80 extends the fresh resource_interfaces.cpp build with GetDesc checks for
buffer/color/depth/staging and resolved full-mip fields. The retained run212 binary
is unchanged and checks only its original interface operations. New independent
GetDesc compiler/run evidence is pending CI and must not be attributed to run212.

The retained resource_interfaces.run.exe is now replaced by the verified run230 artifact,
source tip1f6363d, not an additional fixture:
https://github.com/AKgame47/Anyxbox/actions/runs/45270749667/job/138029577328
503809 bytes; SHA256 `600e7883075d670c83b5864d8621d4f89c37cf9362542efb5fe2b309f06419c0`.
It covers52 imports and includes the GetDesc completion marker. Fixture-slot requires
both markers/exact output and zero graphics activity; CI requires GetDesc in fresh and
retained paths. The earlier run212 notes above describe superseded binary provenance.

Batch82 extends fresh resource_interfaces.cpp with concrete/base view identity and
GetResource checks for RTV/SRV/DSV, including resource use after releasing the original
caller reference and view. Retained run230 is unchanged; new independent evidence
awaits CI and must not be attributed to that older binary.

Retained resource_interfaces.run.exe is now exact run239 output, source tipb0b4965:
https://github.com/AKgame47/Anyxbox/actions/runs/45273708342/job/138039881731
505041 bytes; SHA256 `80ad590dd7ac29ce67a061dc72d107b5d34ef8d375ca0b9afe8d8b951abc7d13`.
This replaces run230 in the same fixture slot. It includes all three descriptor/view/
interface markers. Earlier binary provenance remains historical. Fresh and retained
CI paths require the view marker; fixture-slot requires exact combined output.

Batch84 extends the fresh resource_interfaces.cpp probe with shader bind/caller-release/
same-object-rebind/unbind reference checks. It uses existing hand-built triangle DXBC,
not Microsoft shader output. Retained run239 is unchanged and does not prove these
new calls. Fresh independent evidence awaits CI.

Batch85 replaces the same resource_interfaces.run.exe with the verified run248
artifact from source tip 55e3fa0. This is not an additional fixture.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45276785711/job/138050528948
506647 bytes; SHA256 4fc6a84c5447a20cf862e7a584d02b48654d1b4b216f778027ca8ab6e09a0dcf.
The retained probe now includes shader binding checks alongside the earlier
interface, descriptor and view checks. Fixture-slot requires exact combined output,
zero exit, two translated shaders, zero presents and empty D3D11 error. CI requires
the shader marker in fresh and retained paths. Older provenance above is historical.
Shader creation/translation is not graphics presentation or game compatibility.

Batch86 extends only the fresh public-header source probe with output-merger
RTV/DSV binding reference checks: caller release, same-object rebind, resource
access through the retained view, and null unbind. Retained run248 bytes remain
unchanged and do not prove these new operations. Fresh independent MinGW build
and execution await CI. No new executable fixture or scene is added.

Batch87 replaces the same resource_interfaces.run.exe with exact run257 output,
source tip 15f73fa. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45279424900/job/138059645554
507671 bytes; SHA256 f5a097cd81e3c1414ab3f530a70a036b505c048c832915ef66cc7a42409ee6ff.
It includes all five interface/descriptor/view/shader/output-merger markers.
Fixture-slot requires exact combined output, zero exit, two translated shaders,
zero presents and empty D3D11 error. Fresh and retained CI require the new marker.
The output-merger probe covers caller release/rebind/unbind, not replacement,
teardown or MRT refusal. Those remain interface-unit coverage. Earlier binary
provenance is historical. Runtime lifetime and other ownership limits still apply.

Batch88 extends only the fresh public-header probe with pixel SRV binding checks:
caller release, cross-slot swap of solely context-owned views, same-object rebind
and unbind. Retained run257 stays unchanged and lacks this marker. Fresh independent
MinGW compilation/execution awaits CI. No new scene or executable fixture is added.

Batch89 replaces the same resource_interfaces.run.exe with exact run266 output,
source tip 9896e4a. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45281832644/job/138067989069
508183 bytes; SHA256 45d0197f88ef5fbfbc84b9128e71be1c50491d7783cb4e3500523e724eca8b59.
It includes all six completion markers, adding pixel-SRV caller release, cross-slot
swap, rebind and unbind. Exact combined output, zero exit, two translated shaders,
zero presents and empty D3D11 error are required. Fresh and retained CI require
the pixel-SRV marker. Partial replacement, range refusal and teardown remain unit
evidence, not independent guest evidence. Older binary provenance is historical.

Batch90 extends only the fresh public-header probe with input-buffer binding
checks: vertex caller release/cross-slot swap/rebind and index retain/unbind.
Retained run266 bytes remain unchanged and lack this marker. Fresh independent
MinGW compilation/execution awaits CI. No additional fixture or scene is added.

Batch91 replaces the same resource_interfaces.run.exe with exact run275 output,
source tip 9bdbde5. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45284189590/job/138076095997
509207 bytes; SHA256 b3facf087822b217e98b5d3a85bbcbbb7985586bc90eddd1ca8395898d886523.
It includes all seven completion markers, adding vertex caller release/swap/rebind
and index retain/unbind. Exact combined output, zero exit, two translated shaders,
zero presents and empty D3D11 error are required. Fresh and retained CI require
the input-buffer marker. Partial/index replacement, refusal and teardown remain
unit evidence, not independent guest evidence. Older provenance is historical.

Batch92 extends only the fresh public-header probe with VS/PS constant-buffer
caller release, cross-slot swap, rebind and unbind reference checks. Retained
run275 bytes remain unchanged and lack this marker. Independent MinGW compile
and execution await CI. This is reference accounting, not a new rendered scene.

Batch93 replaces the same resource_interfaces.run.exe with exact run284 output,
source tip d64f396. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45286445973/job/138083890353
509719 bytes; SHA256 7fe9e1feb8df806872e86b6ac1d9aa756132f170232762b0935dd974ca7859a4.
It includes all eight completion markers, adding VS/PS constant-buffer caller
release/swap/rebind/unbind. Exact combined output, zero exit, two translated
shaders, zero presents and empty D3D11 error are required. Fresh and retained CI
require this marker. Partial replacement/range refusal/teardown remain unit
evidence. Earlier binary provenance is historical. No rendering claim is added.

Batch94 extends only the fresh public-header probe with pixel-sampler caller
release/cross-slot swap/rebind/unbind checks. Retained run284 bytes are unchanged
and lack this marker. Independent MinGW compilation/execution awaits CI. No new
scene or executable fixture is added.

Batch95 replaces the same resource_interfaces.run.exe with exact run293 output,
source tip 064c39b. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45288646312/job/138091426598
510231 bytes; SHA256 1643381bca5fd0ace2faa41f549cc57c9c19cd88e4098e6312239cb347b71623.
It includes all nine completion markers, adding pixel-sampler caller release,
cross-slot swap, rebind and unbind. Exact combined output, zero exit, two translated
shaders, zero presents and empty error are required. Fresh and retained CI require
the sampler marker. Partial replacement/range refusal/teardown remain unit evidence.
Earlier provenance is historical; full capacity/validation/null defaults are not proved.

Batch96 extends only the fresh public-header probe with input-layout/rasterizer/
depth-stencil/blend binding caller release/rebind/unbind reference checks.
Retained run293 bytes remain unchanged and lack this marker. Independent MinGW
compilation/execution awaits CI. No additional scene or executable fixture.

Batch97 replaces the same resource_interfaces.run.exe with exact run301 output,
source tip 802731c. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45290418094/job/138097404493
512279 bytes; SHA256 ad03ee3dd7228b03084d05a4c632cb051868f9ffdc62fe5a560e0b73663447e9.
It includes all ten completion markers, adding layout/rasterizer/depth/blend
caller release/rebind/unbind. Exact combined output, zero exit, two translated
shaders, zero presents and empty error are required. Fresh and retained CI require
the copied-state marker. Replacement/teardown remain unit evidence. Earlier
provenance is historical; ignored state parameters/full lifetime are not proved.

Batch98 extends only the fresh public-header probe with ClearState shared VS/PS
constant-buffer release and repeated-clear checks. Retained run301 stays unchanged
and lacks the marker. Independent MinGW compilation/execution awaits CI. This is
not independent coverage of every pipeline default or every released binding kind.

Batch99 replaces the same resource_interfaces.run.exe with exact run313 output,
source tip d437ba3. No additional fixture is added.
Provenance: https://github.com/AKgame47/Anyxbox/actions/runs/45292165689/job/138103397400
512791 bytes; SHA256 34f5b6d5e177d28227715723a0dfe0d73c2af14ef99d0f5ef723bcb7599fad95.
It includes all eleven completion markers, adding repeated ClearState/shared
VS/PS buffer release. Exact output, zero exit, two translated shaders, zero presents
and empty error are required. Fresh and retained CI require the ClearState marker.
Other binding-category release remains unit evidence; rendered defaults/backend
reuse are not established. Earlier provenance above is historical.

Batch100 extends only the fresh probe with VSGetShader/PSGetShader identity and
added-reference checks after caller release; returned references survive ClearState,
then getters return NULL. Dynamic shader classes are unsupported, count zero.
Fresh probe now creates four shaders total, not two. Retained run313 bytes remain
unchanged and lack this marker. Independent MinGW build/execution awaits CI.
