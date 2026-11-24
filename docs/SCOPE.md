# Scope

In scope: running x86-64 programs you built yourself or that are open-source homebrew, by translating their system calls to a PC.

Out of scope, permanently:
- Breaking or bypassing Microsoft encryption, signing, licensing or DRM.
- Using game dumps, extracted keys, or pirated copies, or pointing to places that host them.
- Bundling Microsoft binaries or firmware.
- Contacting Microsoft services in unofficial ways.

Testing uses small programs compiled from source in this repo (a console triangle, a textured quad, an input/audio demo).

## Real-shader validation (open gaps)
Real fxc blobs from Wine's tests (LGPL-2.1+, sources in tests/real_dxbc_wine_tex.h) validate: sample, dcl_resource/sampler, mul immediate, SV_POSITION input, ieq/if_nz/endif with a PS cbuffer.
Relative cbuffer indexing and multiple cbuffer slots: see the update below.

Update: the relative-addressing VS now translates (relative cbuffer indices, several cbuffer slots, vertex uniform buffer at set 1 binding 1,
packed slot layout described in src/shader/dxbc_spirv.h) and renders correct colours for indices 0..7 in test_real_dxbc_tex.
Still open: indices past the declared register count (Wine's 8 and 9), and the COM runtime does not yet pack several VS/PS slots for the translator.

## Window layer and user32 (batch 24)
- `src/runtime/host_window.*`: display/input backend. The SDL2 backend loads libSDL2 with dlopen (no headers, no link flag). With no SDL or no display, `open()` fails and the runtime stays headless.
- `src/runtime/user32.*`: RegisterClass(Ex)A, CreateWindowExA (runs WM_CREATE), ShowWindow (opens the host window), message queue (Peek/GetMessage, Dispatch calls the guest window procedure natively), DefWindowProc, PostQuitMessage, PostMessage, SendMessage, GetClientRect, MessageBoxA (recorded, never shown), GetAsyncKeyState (last key only). Not covered: wide-char (W) variants, timers, menus, GDI, child windows, real frames or focus.
- Verified under Xvfb: an RGBA frame presented through the SDL backend appears on the X screen with the right colours (screenshot compare). No game has used it.
- `tools/import_gap.cpp` lists, for any PE, which imports have a host implementation. `tests/fixtures/` is the slot for self-built real programs.

## Fixture, present path and startup basics (batch 25)
- `window_present.h`: `attach_window_present(runtime, user32_ctx)` sends each swap-chain Present to the host window.
- `tests/fixtures/triangle.c`: self-written freestanding D3D11 triangle for MinGW; built and inspected by a read-only GitHub Actions job. `tools/run_exe.cpp` runs a self-built exe (`--i-built-this`, optional `--window`).
- user32 wide-character variants (ASCII only; other characters become `?`). kernel32: Get/SetLastError, TLS (64 slots, per thread), process heap, GetTickCount, GetSystemTimeAsFileTime, process/thread ids, IsDebuggerPresent, SetUnhandledExceptionFilter, GetCommandLineA, OutputDebugStringA.
- Still missing for a typical MinGW C runtime startup: `__getmainargs`, `_initterm`, `_onexit`, `__set_app_type`, `signal`, `_cexit`, `__C_specific_handler`, `RtlVirtualUnwind`, `GetStartupInfoA`, `GetModuleHandleW`, `GetProcAddress`/`LoadLibrary`, `VirtualProtect`/`VirtualQuery`, `EnterCriticalSection` on static data, `FlsAlloc` and others. The fixture avoids the runtime on purpose; CI's import_gap output will show the real list for any other exe.

## C runtime startup set (batch 28)
Driven by the CI import_gap report for a default-startup MinGW program (tests/fixtures/hello_crt.c, 35 missing imports, run #15). Now implemented: the six kernel32
and 29 msvcrt names from that report (see the header comments of src/runtime/msvcrt.h and kernel32.cpp). `exit`, `abort` and `_amsg_exit` end the guest through the
same path as ExitProcess. VirtualProtect/VirtualQuery only touch the main image and VirtualAlloc regions. stdio exists only for the three standard streams.
Not covered: a TLS directory (callbacks and __declspec(thread) data are ignored by the loader), SEH unwinding (`__C_specific_handler` is a stub that never handles anything),
signals, wide-character stdio, file streams (fopen and friends), locale beyond the C locale. Whether hello_crt.exe really runs is decided by the next CI run, not by these unit tests.

## Thread environment block (batch 30)
The first real MinGW program (hello_crt.exe, CI run #20) faulted on `mov rax, gs:[0x30]` in its own startup code. Each guest thread now gets a fake TEB and the process a shared PEB
(`src/runtime/teb.*`, arch_prctl ARCH_SET_GS): Self, StackBase/StackLimit from the host thread stack, ClientId, ThreadLocalStoragePointer (an empty table), PEB with image base, empty process
parameters, heap handle and a Windows 10 version. Everything else in those structures reads zero. Linux x86-64 only. Result: hello_crt.exe and triangle.exe, both built by CI from our own sources,
now run end to end (hello prints its line; triangle presents 3 frames with the expected pixels). They are committed as tests/fixtures/*.run.exe and run by test_fixture_slot.
Still not covered: the TLS directory (callbacks, __declspec(thread) data), SEH unwinding, DLL loading (LoadLibrary/GetProcAddress), real Xbox binaries.

## Batch 31: scene fixture

tests/fixtures/scene.c is a self-written freestanding program that uses a depth buffer, an index buffer, a dynamic constant buffer updated with Map between draws, and a 2x2 texture. test_fixture_scene runs the same C file host-compiled against the runtime and checks pixels on the software Vulkan driver. The MinGW build and the real exe run are decided by CI. run_exe gained --gpu (Vulkan backend) and --save-frame (PNG). The CI source list now includes src/runtime/teb.cpp.

## Batch 32: real scene binary

tests/fixtures/scene.run.exe is the exe MinGW built in CI from tests/fixtures/scene.c (SHA-256 d24dd950f792fe8f9cbe6aada629b554dda468a12e43fda1228d5f7b4b230202). test_fixture_slot runs it and, when a Vulkan device exists, checks the frame pixels. The frame rendered here matches the CI frame byte for byte.

## Batch 33: PE TLS directory

The loader reads data directory 9 (template, index slot, callbacks), validates every address against the image, and writes TLS index 0. Each thread that gets a TEB also gets its own copy of the template (raw bytes plus zero fill) at gs:[0x58][0]. TLS callbacks run with DLL_PROCESS_ATTACH before the entry point. Not done: thread attach/detach callbacks, more than one module's TLS, TlsAlloc slots inside the TEB. tests/fixtures/tls_crt.c is a MinGW __thread program that CI builds; it is not committed as a .run.exe until CI shows it runs.

## Batch 35: LoadLibrary / GetProcAddress, TLS binary

LoadLibraryA/W, GetModuleHandleA/W, GetProcAddress (by name or ordinal) and FreeLibrary work against the DLL sets Anyxbox itself provides (kernel32, user32, d3d11, msvcrt, ...). The module handle is a fake address per set; GetProcAddress returns the same function address an import of that name would get. A DLL Anyxbox does not provide fails with ERROR_MOD_NOT_FOUND (126); a missing function fails with ERROR_PROC_NOT_FOUND (127). No file is ever loaded from disk, so a game's own or third-party DLLs are not loaded.
tests/fixtures/tls_crt.run.exe is the CI-built TLS program from run #44 (SHA-256 985a39579198e5e13ce6402c86366e862b048db19e12fe00bb5c2c2930b8ba3f); it prints "tls=42 zero=0".

## Batch 36: XInput and XAudio2 stubs

xinput1_4/1_3/9_1_0: XInputGetState, SetState, GetCapabilities, Enable, GetBatteryInformation, GetKeystroke. With no pad connected they return ERROR_DEVICE_NOT_CONNECTED, as a PC without a controller does. The host can connect a pad (XInputContext::set_pad); wiring real controllers (SDL) is not done.
xaudio2_9/2_8: XAudio2Create with IXAudio2 (2.8+ vtable), source, submix and mastering voices. Buffers are accepted and counted as played immediately; no sound is produced, buffer-end callbacks are not called, and effects, filters and 3D audio are not implemented. XAudio2 2.7 (COM class registration) is not covered. Both are registered by run_pe for every program.

## Batch 37: structured exceptions (software)

The loader records the exception directory (.pdata). src/runtime/seh.cpp implements the table-based x64 unwinder: RtlLookupFunctionEntry, RtlVirtualUnwind (push, alloc, frame register, saved registers and xmm, chained info, machine frames), RtlCaptureContext, RtlRestoreContext, RtlUnwind, RtlUnwindEx, RaiseException, and __C_specific_handler (C scope tables: __try/__except/__finally as MSVC and MinGW emit them). Each frame's handler is called with the real exception record and a CONTEXT, two phases like Windows. An exception nobody handles ends the program with the exception code.
Done by test with a hand-assembled image: caught exception with __finally run during unwind, literal and function filters, CONTINUE_SEARCH, CONTINUE_EXECUTION.
Not done: hardware faults (access violation, divide by zero, illegal instruction) do not become guest exceptions yet; vectored handlers; SetUnhandledExceptionFilter callbacks are not called; nested exceptions in filters; epilogue-aware unwinding; unwinding across CreateThread entry; C++ exceptions are untested here because the sandbox has no MinGW (they use the same Rtl* functions from the program's own libgcc and still need a CI fixture).

## Batch 38: hardware faults as guest exceptions, C++ exception fixture

A SIGSEGV/SIGBUS (access violation 0xC0000005 with read/write flag and address), SIGFPE (integer divide by zero 0xC0000094, others) or SIGILL (0xC000001D) whose instruction pointer is inside a loaded guest image becomes a guest exception: a CONTEXT and EXCEPTION_RECORD are built on the guest stack and the thread resumes in the dispatcher, so the same handlers, filters and __finally blocks run. Faults anywhere else go to the previous signal handler. Not done: stack overflow (no alternate stack), faults in the middle of a host function called by the guest, and flags beyond the common registers in the CONTEXT.
tests/fixtures/eh.cpp is a MinGW C++ throw/catch program built statically in CI. It has not run yet: CI decides whether libgcc's unwinder works on top of the Rtl* functions.

## Batch 39: registry reconciliation, five more msvcrt functions

import_gap and test_fixture_slot now register the same function sets as run_pe (including the exception functions), so a coverage report matches what run_exe can resolve. Added msvcrt memchr, fputs, _write, _read, _close (descriptors 0, 1 and 2 only: stdin is always at end of file, output is captured). Whether eh.exe runs is still decided by CI.

## Batch 40: C++ exceptions through the SEH layer
- Entry stubs for RaiseException, RtlCaptureContext, RtlUnwindEx and RtlUnwind align the stack themselves; guest code may call them misaligned.
- Host frames of the dispatcher have no unwind tables, so a walk that reaches one continues from a saved continuation context (the walk that called the handler). This lets libgcc's nested RaiseException/RtlUnwindEx (cleanups, catch) find the frames above.
- ANYXBOX_SEH_TRACE=1 prints each dispatch and unwind step to stderr.
- The CI-built eh.exe (static MinGW libstdc++) runs locally: three destructors, "caught boom", "ok".

## Batch 42: vectored handlers and the unhandled-exception filter
- AddVectoredExceptionHandler / AddVectoredContinueHandler / RemoveVectoredExceptionHandler / RemoveVectoredContinueHandler, SetUnhandledExceptionFilter and UnhandledExceptionFilter (kernel32 and ntdll names).
- Order: vectored handlers, then frame handlers, then the filter, then the program ends with the exception code. A handler or the filter returning EXCEPTION_CONTINUE_EXECUTION resumes the program (for hardware faults at the context, which the handler may have edited).
- Handlers and the filter are cleared when a program ends.
- Not done: a context edit by a handler during a software RaiseException is ignored (execution resumes after the call); the filter's second-chance semantics for a debugger are not modeled.
- New fixture source tests/fixtures/veh.cpp (built by CI only until its exe is committed).

## Batch 43: CI runs the unit tests
- fixture.yml now compiles and runs every tests/test_*.cpp against the CI object list (strict, no `|| true`), so the committed fixture assertions (test_fixture_slot) are checked by CI. veh.run.exe is committed.

## Batch 44: safe signal-to-exception handoff
- The hardware-fault signal handler previously wrote a context and exception record below the interrupted stack pointer, overlapping memory Linux may still be using for the active signal frame and handler stack.
- It now saves those records in thread-local storage. After sigreturn completes, fault_dispatch copies them into its own locals before calling guest handlers. No writes to the interrupted stack occur in the signal handler.
- test_seh adds a hardware VEH case that edits the saved instruction pointer and resumes after the faulting instruction.
- Strict CI test failures print the full test log, and test logs upload even on failure. An additional strict test builds the SEH layer and its test with explicit stack protection.
- The prior CI crash could not be reproduced locally. This fixes the stack overlap found during review; confirmation on that runner remains required.

## Batch 45: thread TLS callbacks
- CreateThread installs the new thread's TEB and fresh TLS template, runs the image callback list with DLL_THREAD_ATTACH (2), calls the thread entry, then runs that list with DLL_THREAD_DETACH (3) before restoring the host GS base.
- Callbacks run in list order. The completion handle becomes signaled after detach callbacks finish. tests/test_teb checks two workers, callback ordering, exit status, fresh raw/zero-fill TLS, and isolation from the main thread's modified TLS.
- New CI-built source tls_thread.c checks callbacks and per-thread data through actual MinGW startup and CreateThread. Its binary has not yet been verified or committed.
- Remaining limits: only normal thread-entry return gets detach callbacks. ExitThread, abrupt termination, process-wide shutdown of still-running workers, callback serialization with process startup, and multi-module TLS remain unsupported. Guests must join their workers before returning from the main entry. No new claim of Xbox game compatibility.

## Batch 46: committed thread TLS fixture
- tls_thread.run.exe is the verified CI-built thread TLS fixture. test_fixture_slot checks its exact output and successful exit alongside its 60 resolved imports.
- This checks normal thread return with the main thread waiting for both workers. Other shutdown paths remain outside the supported scope described above.

## Batch 47: optional SDL2 controller input
- run_exe --controller runtime-loads SDL2's GameController API, discovers up to four mapped controllers, and refreshes their state when XInputGetState or XInputGetCapabilities is called. No SDL headers or link-time SDL dependency are required.
- Standard buttons map to XInput bits; thumbstick Y is inverted with saturation and trigger values scale to 0..255. Controller slots retain their instance IDs while connected, disconnect on removal, and reopen for new devices. Packet numbers change only when the reported state changes. XInputEnable(false) returns neutral button/axis state.
- test_devices uses an SDL virtual controller to verify A/d-pad, stick axes, full trigger, unchanged packet, and disconnect through XInput entry points. This integration passed locally with SDL2. CI installs the SDL2 runtime; the test explicitly reports a skip if the runtime/virtual API is unavailable.
- Limits: physical controller operation has not been tested. Rumble remains recorded only, not delivered to hardware; keystrokes and accurate battery status are not implemented. Only SDL-mapped game controllers are supported. This is opt-in; default runs leave pads disconnected. SDL and guest calls remain subject to SDL's host thread/event-loop constraints; cross-thread polling is not a proven path.

API references for this backend:
- https://wiki.libsdl.org/SDL2/SDL_GameControllerGetAxis
- https://wiki.libsdl.org/SDL2/SDL_GameControllerAxis
- https://wiki.libsdl.org/SDL2/SDL_GameControllerButton
- https://wiki.libsdl.org/SDL2/SDL_GameControllerUpdate
- https://wiki.libsdl.org/SDL2/SDL_JoystickAttachVirtual

## Batch 48: D3D11 scissor rectangles
- CreateRasterizerState accepts ScissorEnable. RSSetScissorRects (context vtable slot 45) binds zero or one rectangle; zero resets to the default empty rectangle. Multiple rectangles and inverted bounds produce a diagnostic, not silent truncation.
- Reference and Vulkan drawing obey the enabled rectangle; bounds clip to the target, right/bottom are excluded, and clear operations remain independent of the scissor. The Vulkan render area stays the full target so clearing still initializes pixels outside the scissor.
- Six model cases check edges, empty/default state, disabled scissor, negative bounds, an entirely off-target rectangle, and moving scissors across successive draws while preserving prior contents. They compared against local software Vulkan with zero pixel mismatches. A hand-built PE tests the COM state object and context call.
- CI installs the software Vulkan driver before strict tests, rather than only for later fixture rendering. Check logs for actual device execution; physical GPU validation remains absent.
- API grounding: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetscissorrects and https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-getting-started

## Batch 49: deterministic protection-test control
- The writable-code control in test_protections returned without setting EAX. The child classified return codes starting with 0xC as faults, so an undefined return value could make this allowed-write control fail intermittently. It now explicitly returns zero.
- Child results now log load/run state and exit code; fork and wait failures are fatal diagnostics rather than accidental passes. Existing read-only-code and non-executable-data assertions remain unchanged.
- This identifies a test defect consistent with the prior local intermittent failure, not a captured reproduction proving its cause. The corrected test passed 200 consecutive local ASan/UBSan runs.

## Batch 50: Vulkan viewport offsets, size and depth range
- The Vulkan draw path uses the bound viewport instead of rejecting every viewport smaller than the render target. RSSetViewports now carries MinDepth/MaxDepth through the model and backend.
- Reference depth interpolation maps the clipped 0..1 depth into the viewport depth range before testing/storing it. Non-finite viewports and invalid depth ranges are rejected.
- Model tests compare offset/reduced-size drawing and a depth-range-dependent visibility result against local software Vulkan, with zero mismatches. A hand-assembled guest PE checks viewport placement through COM.
- One viewport only; geometry-stage viewport selection, full clipping, physical GPU validation, and all device-specific viewport-limit edge cases remain outside this slice.
- API grounding: the viewport scale/depth equations in https://learn.microsoft.com/en-us/windows/win32/direct3d11/d3d10-graphics-programming-guide-rasterizer-stage-getting-started

## Batch 51: texture mip storage and Vulkan t0 sampling
- RGBA8/BGRA8 texture resources keep separate mip levels, including a full chain for MipLevels=0. COM texture initialization reads one pitched subresource per level. UpdateSubresource and Map/Unmap address mip indices; the mapped mip is tracked to avoid writing a different subresource on Unmap.
- Full-chain shader-resource views are accepted; subset views and nonzero-mip render-target views fail explicitly. Sampler mip filtering and MinLOD/MaxLOD feed the Vulkan sampler. Nonzero LOD bias remains rejected.
- The t0 Vulkan path packs and uploads the chain. Higher texture slots still reject mip chains explicitly. The reference renderer does not interpret sampled shader code, so sampling validation uses translated DXBC SampleLevel on local software Vulkan with distinct solid colors at each mip.
- Five checks prove explicit levels 0/1/2, fractional linear mip filtering (green-blue midpoint), and MinLOD clamping. COM tests verify pitched initialization, Map per level, subresource update, and unsupported subset-view rejection. All 32 test programs pass under local ASan/UBSan after a full rebuild.
- Limits: t0 only for multi-level sampling; no automatic GenerateMips, subset SRVs, arrays, compressed formats, nonzero-mip rendering or physical GPU validation. Vulkan upload retains the 256x256 texture limit. No real game compatibility claim.
- API grounding: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_texture2d_desc , https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_tex2d_srv , https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_sampler_desc

## Batch 52: mip sampling across t0..t3
- Additional Vulkan texture bindings upload their packed mip chains, expose all levels in the image view, and use the selected sampler's mip filter and LOD bounds.
- The five explicit-level/filter/clamp cases from batch 51 now run separately at each supported texture slot, twenty actual software-Vulkan sampling checks in total. Higher slots also exercise zero-filled lower unbound slots. All passed locally along with all 32 ASan/UBSan test programs.
- This removes the t0-only restriction for mip sampling. Remaining limits stay: four texture slots, 256x256 Vulkan upload limit, no GenerateMips, no subset views, no compressed texture formats or arrays, and no physical GPU or real game compatibility validation.

## Batch 53: GenerateMips for RGBA8/BGRA8
- Context vtable slot 54 generates the full SRV's lower levels recursively from its base level when the texture was created with both render-target/shader-resource bind flags and GENERATE_MIPS. Missing flags leave it unchanged, matching the documented API gate.
- The model uses a CPU area-weighted box reduction, including odd final rows/columns, per stored channel. This is a chosen filter, not a promise of bit-exact agreement with every D3D driver. RGBA8/BGRA8 UNORM only; sRGB is not supported.
- Tests check odd dimensions, all channels, recursive levels, regeneration after base edits, invalid IDs and COM flag gating. A generated checkerboard chain is sampled through translated DXBC on software Vulkan at each t0..t3 slot; all return the expected reduced color. Total mip sampling checks: 24.
- No GPU compute generation, subset views, arrays, compressed formats or physical GPU validation. Existing resource limits still apply.
- API grounding: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-generatemips

## Batch 54: mip-subset shader-resource views
- Texture2D SRVs validate MostDetailedMip and level count, including all-remaining-levels (-1). Bound sampling rebases dimensions and LOD zero to the selected first mip and exposes only the selected chain. Works across t0..t3.
- GenerateMips starts from the selected view's top mip and stops at its last mip, preserving levels outside that range. The same format/flag restrictions remain.
- Eight sampling modes at each supported slot, 32 total local software-Vulkan checks. New modes prove LOD-zero rebasing to mip 1 and out-of-view LOD clamping. COM tests verify valid/invalid ranges and generation confined to levels 1..2 while levels 0 and 3 remain unchanged.
- Limits still include UNORM RGBA8/BGRA8, four slots, 256x256 Vulkan upload size, no arrays/compressed formats/nonzero-mip RTVs, and no physical GPU or game compatibility validation.
- View semantics: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_tex2d_srv . Generation range: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-generatemips

## Batch 55: host-backed resource copies
- Context CopyResource (47) copies buffers or identically sized/formatted UNORM color textures including all mip levels. CopySubresourceRegion (46) copies byte ranges or pitched texel rectangles between supported subresources, without scaling or color conversion.
- Same-resource whole copies, same-subresource region copies, mapped resources, immutable destinations, mismatched types/formats, invalid mip indices and out-of-bounds regions fail with diagnostics. Empty source boxes do nothing. Arithmetic checks avoid unsigned destination overflow.
- Copies complete synchronously in host storage, not an asynchronous GPU queue. Render-target destinations are explicitly unsupported because persistent Vulkan render surfaces are not re-seeded from modified host pixels. Depth copies, arrays and format reinterpretation remain unsupported.
- COM tests compare all bytes for whole mip chains and buffer copies, verify region rows and byte offsets, and exercise mapped/same-resource/bounds/empty-box behavior. All 32 local ASan/UBSan tests pass.
- API grounding: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copyresource and https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-copysubresourceregion

## Batch 56: host-write synchronization for Vulkan color targets
- The Vulkan draw backend detects color bytes changed since its previous draw and stages those bytes into the persistent color attachment before drawing again. Initial host contents are also preserved on the first draw. Existing depth contents survive a color-only replacement.
- Color image allocations include transfer-destination usage. Replacement transitions/copies happen before the render pass, which loads the uploaded color instead of clearing it. The backend retains the most recent host pixels for change detection; this adds CPU memory/comparison overhead and is not a performance-ready design.
- Copy destinations may now be render-target color textures. Map/Unmap and UpdateSubresource writes use the same host-pixel detection path. Copies are still synchronous host-storage operations; depth copying and format reinterpretation remain unsupported.
- Local software-Vulkan/reference tests check host replacement before/after the first draw and preserved depth after replacement, all with zero pixel mismatches. COM copy tests now use render-target-capable textures. All 32 local ASan/UBSan tests pass. Physical GPU validation remains absent.

## Batch 57: COM-to-Vulkan write/copy regression coverage
- Eight integration cases join the previously separate COM copy and Vulkan synchronization tests. A live Vulkan color target is first drawn, then changed through CopyResource, CopySubresourceRegion, UpdateSubresource or Map/Unmap; a smaller second draw must preserve the changed background. Each path runs for RGBA8 and BGRA8, with final pixels read through COM Map.
- These are host tests using Windows-ABI vtables plus actual local software Vulkan, not new independently compiled guest binaries. Both channel orders and all four mutation paths pass. All 32 ASan/UBSan test programs pass. Production behavior and remaining limits are unchanged from batch 56.

## Batch 58: separately compiled texture-copy fixture source
- CI builds texture_copy.cpp using MinGW's D3D11 headers and calls COM methods from that independently compiled Windows executable. It checks whole mip-chain copies, region copies, staging Map readback and generated mip copying.
- The fixture uses proper default-source and staging-readback resource descriptors. It prints four success lines and exits zero if all checks pass. Its build/run step is strict.
- No local MinGW toolchain is available, so compilation/execution of this new fixture awaits CI. It is not yet a committed executable regression. This adds no production functionality and makes no new compatibility claim.

## Batch 59: committed independently compiled texture-copy regression
- Added the exact texture_copy.exe from CI run 141 as texture_copy.run.exe, with recorded source commit, build command and SHA-256 in the fixture README.
- test_fixture_slot now requires its four exact output lines, successful exit, empty runtime error, zero presented frames and zero translated shaders. The independent Windows binary passed locally with address/undefined-behavior sanitizers after relinking run_exe to the current runtime objects.
- This is copy/readback coverage only. It does not add drawing or prove physical-GPU or game compatibility.

## Batch 60: draw/copy/present fixture source and strict pixel verifier
- Added a scene-source variant that presents once, copies yellow into its sampled texture and cyan into the already-drawn backbuffer, then draws/presents again without another color/depth clear. LESS_EQUAL allows the existing geometry to redraw while retaining the near-over-far overlap.
- Added test_fixture_draw_copy: host-source mode runs locally; independent-executable mode loads CI's MinGW binary. Both modes check two presents, four shader translations, no runtime error, first-frame texture colors and second-frame cyan background/yellow texture/green-orange geometry.
- CI requires Vulkan for independent-binary validation and retains both presented frames. Local sanitizer host-source checks passed on software Vulkan; both images were inspected. The separately compiled binary still awaits CI. No physical-device or game compatibility claim.

## Batch 61: retain independent draw-copy binary regression
- Committed the exact run-149 draw_copy.exe as draw_copy.run.exe, with build/source/hash provenance in the fixture README.
- The draw-copy test's directory mode now checks source calls and the retained PE. CI requires Vulkan separately for both the freshly built and retained executables; frame images for both are retained.
- Local sanitizer run of the supplied independent binary passed all two-frame pixel checks. Both local frames matched the supplied CI frames byte-for-byte as decoded RGB images; inspected both CI images. The fixture-slot check also requires the expected present/shader counts and no output/error.
- Scope remains software Vulkan validation of this self-written fixture, not game or physical-device compatibility.

## Batch 62: independent draw variants for region-copy, update and map writes
- Added three MinGW fixture sources and three test wrappers using the existing draw-copy verifier.
- Region-copy and UpdateSubresource change the right texture column and a 20-row top strip of the live target. Pixel checks cover changed and unchanged regions in both frames.
- Map writes the dynamic sampled texture directly and a staging resource for CopyResource into the target. It does not map a default backbuffer or use UpdateSubresource on a dynamic resource.
- Local sanitizer host-source paths passed on software Vulkan; inspected all six captured frames. CI's new independent binary checks require Vulkan and retain the binaries/images. New independent compile/run results await CI. Production functionality unchanged.

## Batch 63: retained independent region/update/map regressions
- Committed the exact three run-159 binaries with source/build/hash provenance in the fixture README.
- Directory-mode variant tests now run both host calls and the corresponding retained PE. Strict CI separately requires Vulkan for fresh and retained variants, and retains all frame images.
- Fixture-slot test covers twelve committed executables and checks expected two-present/four-shader/empty-output/error behavior for all draw variants.
- All six local independent-binary frames matched decoded CI RGB bytes exactly; inspected the six CI frames. Validation remains this self-written fixture on software Vulkan, not physical hardware or games.

## Batch 64: explicit read-only launcher asset root
- run_pe accepts RunOptions::asset_root; run_exe exposes --asset-root DIR. Default remains no file access. Invalid roots fail before execution; valid roots opt into read-only regular-file access.
- MappedFolder opens guest components descriptor-relative with no symlink following; final objects must be regular files. Read-only mode blocks create/write opens. Existing host-configured writable modes remain for prior tests, with existing OPEN_ALWAYS/append limitations unchanged.
- Synthetic tests cover component/final symlink refusal, no external truncation, FIFO rejection, read-only opens and actual launch-route default denial/opt-in reading/truncating-write denial. All 36 local sanitizer programs pass after rebuilding all tests for the changed run_pe signature.
- Added an independent file-backed MinGW fixture and strict CLI tests, not yet compiled/executed outside CI. No external guest binary or private host file used.
- This routing does not isolate native guest code. Host-selected root topology/contents remain trusted; no OS sandbox claim. See ASSET_IO.md for scope and limits.

## Batch 65: retained file-backed launch regression
- Committed the exact CI run-173 asset_read.exe with source/build/hash provenance. Local sanitizer execution verified exact read output, zero exit with explicit root and exit 10 without root.
- Fixture-slot now checks thirteen retained programs. Its asset case creates only synthetic temporary files, checks explicit-root output, source asset unchanged and default denial. CI separately runs fresh/retained binaries through the CLI, both rooted and default-denied.
- All 36 local sanitizer tests pass. Production code unchanged; read-only path routing still does not isolate native code or provide writable saves.

## Batch 66: file-backed 512 texture target and bounded larger sampling
- Added asset_scene.c, which reads a generated 512x512 RGBA asset through the explicit read-only root, uploads it and presents the scene twice. Local host-source mode checks both frames on software Vulkan. Independent MinGW compile/run awaits strict CI.
- Raised the sampled-texture policy cap from 256 to 1024 per side, at all four slots. Packed mip metadata/copy arrays now support eleven levels; tests sample the final level of a full 1024 chain separately at each slot.
- Before staging upload or image creation, query vkGetPhysicalDeviceImageFormatProperties for the exact format/type/tiling/usage/flags and validate extents, mip/layer counts, sample count and logical bytes. Actual Vulkan create/allocation/bind failures remain checked. These capability checks do not guarantee allocation success.
- Limit logical texture storage/upload to 64 MiB per resource. The model rejects larger host allocations before allocation; D3D11 supplies exact packed storage length to the Vulkan layer, which rejects short input before memcpy. Legacy raw Vulkan callers may omit byte_size and still own pointer-length correctness. Bounded dimensions/mips/layers/depth keep byte arithmetic within 64-bit size_t.
- Tests reject short storage, over-cap extents, excessive mip counts and upload-budget excess. Device-specific rejection branches beyond the local driver's reported capabilities are not yet exercised by a synthetic driver. Scope remains RGBA/BGRA D3D11 2D sampling, software Vulkan, not games or physical devices. The 1024 cap is policy, not an advertised hardware maximum.

Capability references: https://docs.vulkan.org/refpages/latest/refpages/source/VkImageFormatProperties.html and https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties.html

## Batch 67: retained independent file-backed texture scene
- Retained the exact run-187 asset_scene.exe with hash/source provenance. Local sanitizer execution passed both-frame Vulkan pixel checks; decoded local frames matched CI RGB bytes exactly.
- Directory-mode verifier covers source calls and retained executable. CI requires Vulkan for fresh/retained binaries. Fixture-slot covers fourteen retained programs, checking missing-root exit40 and successful execution with generated assets.
- No production changes. Existing device-rejection and legacy raw-pointer caveats from batch 66 remain. Software-only fixture evidence, not game or physical-device compatibility.

## Batch 68: sampled image capability rejection paths
- Split the production device-capability diagnostic into query failure, width, height, depth, mip count, array layers, sample count and resource-byte reasons.
- A test-only friend accessor temporarily replaces the resolved image-format query, while the remaining Vulkan operations use the real software driver. Sixteen production render calls independently reject each condition at t0 and at the extra-texture path (t1).
- A wrapped real vkCreateImage counts sampled allocations: the rejected sampled image is never created; t1 cases create only the preceding valid t0 image. Queries validate expected format/type/tiling/usage/flags. The original query is restored and a real-driver-query render succeeds afterward.
- CI explicitly requires Vulkan for this check. Injected rejection results test validation logic, not proof that a physical device reports those limits. Device allocation exhaustion, native-GPU execution and legacy raw-pointer bounds remain outside this evidence. No limit increase or game-compatibility claim.

## Batch 69: keyboard movement for the file-backed homebrew scene
- GetAsyncKeyState high-bit subset tracks simultaneous held keys, releases and repeated keydown idempotently. Polling pumps pending host events. Focus loss and close clear held state; SDL focus-lost events reach the translated state.
- Added keyboard_scene.c: arrow keys move the green quad, Escape exits, file-backed texture remains visible. Default four frames; an explicit FRAMES build override permits a bounded interactive run. No release build.
- Host-source test scripts right-down, right-up and focus loss between presents, checking movement then identical stopped frames. CI independently compiles/runs the same fixture with required Vulkan; that result awaits CI. Synthetic host events do not prove physical keyboard behavior.
- GetAsyncKeyState still implements only current-state high bit for mapped host keyboard codes. Low transition bit, mouse-button polling, left/right modifier distinction, multiwindow focus and OS desktop semantics are not claimed.

Input references: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate and https://wiki.libsdl.org/SDL2/SDL_WindowEventID

## Batch 70: retained independent keyboard homebrew regression
- Retained run-202 keyboard_scene.exe exactly, with source/build/hash provenance. Local independent PE execution passes scripted movement/release/focus pixel checks and all four frames match decoded CI image bytes.
- Directory-mode test runs source calls and retained binary. Required-Vulkan CI checks fresh and retained independent programs. Fixture-slot covers fifteen programs including keyboard-scene default-root denial and successful rooted execution.
- No production changes. This remains scripted host-event evidence, not physical-keyboard, native-GPU or game compatibility validation. No release build.

## Batch 71: real SDL event route for the existing keyboard scene
- Added a test wrapper around the existing scene/verifier, not another guest scene. A real SDL window opens/presents all four frames. SDL_PushEvent sends synthetic right-down/up/focus-loss through production SDL_PollEvent translation and user32 polling, then the existing movement/stopped-frame pixel assertions run.
- Runs source calls and retained independent PE; strict CI additionally runs freshly compiled and retained executables with Xvfb and required Vulkan/SDL. Test clears unrelated Xvfb events between presents before injecting its sequence.
- No production changes. This is synthetic SDL event-route coverage with software Vulkan, not physical keyboard or uncontrolled desktop-focus behavior. Frame capture verifies rendered pixels, not screenshot parity of the SDL window.

## Batch 72: resource interface discovery, not another scene

The existing D3D11 device/context, buffer, color texture and depth texture now answer
QueryInterface for their concrete interface, IUnknown and the applicable supported
ID3D11DeviceChild/ID3D11Resource base interfaces. Successful queries preserve object
identity and add one reference. Unsupported queries clear the output and return
E_NOINTERFACE. Device children and the immediate context expose GetDevice with an
added reference. Buffer/texture GetType reports BUFFER/TEXTURE2D. Separate resource
and generic-child vtables keep resource slot 7 from colliding with other child methods.
Swapchain GetBuffer now respects the requested interface instead of returning a texture
for arbitrary IDs. The old hand-built state test now supplies a real texture IID.

Local ASan/UBSan regression: all 41 test programs pass, including a source-call
interface matrix, failed/null query paths, balanced query/GetDevice reference deltas,
color/depth/buffer type checks and GetBuffer interface acceptance/refusal. Leak checks
remain disabled, as in prior batches. This does not establish complete COM object
lifetime correctness. Child objects still do not keep their creating device alive;
state-binding and resource destruction ownership remain incomplete. Runtime ownership
keeps device/context storage alive during the guest run. No thread-safety claim.

The public-header resource_interfaces.cpp probe is a new independently compiled PE
CI requirement, not a new visual scene. It requests the same supported identities and
checks type/GetDevice/unsupported-interface behavior. Local Windows cross-compilation
is unavailable, so independent compiler/binary execution remains pending CI. Other
concrete child interface IDs, DXGI IDs, descriptor/private-data/eviction-priority
methods and versioned interfaces remain outside this slice. GetDevice/GetType are
implemented, not a claim that every method in the advertised base interface works.

Interface IDs were checked against public mingw-w64 d3d11.h:
https://github.com/mingw-w64/mingw-w64/blob/master/mingw-w64-headers/include/d3d11.h
API behavior references:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicechild-getdevice
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11resource-gettype

## Batch 73: retain independent resource interface regression

The exact run-212 resource_interfaces.exe is retained with source/hash provenance.
The fixture-slot test now runs sixteen independent retained programs and requires the
resource probe's exact output, zero exit, no graphics activity and empty D3D11 error.
CI keeps fresh/retained interface probe execution separate and strict. All 41 local
ASan/UBSan test programs pass with this independent binary. No production change,
new scene or new test target. The batch-72 lifetime/device-retention/ownership and
thread-safety limits remain, including disabled leak checking in the local suite.

## Batch 74: fail closed on wrapped PE metadata

The loader now computes relocation targets and import thunk offsets in 64 bits before
checking the image span. It rejects a non-ordinal 64-bit import-name entry that cannot
fit an RVA, instead of truncating it or wrapping the two-byte hint skip into headers.
Import descriptors must fit their declared directory, which must itself fit the image;
a terminator requires all five descriptor fields zero. Nonterminal descriptors need a
name and IAT. OriginalFirstThunk=0 continues to use the IAT lookup fallback.
Relocation block lengths use subtraction bounds and must contain complete 16-bit
entries; truncated trailing block bytes are rejected. Null input is rejected by the
mapper and image-size helper without dereferencing it.

Expanded existing test_pe_image with 16 negative checks and a positive fallback check:
wrapped DIR64/HIGHLOW targets, oversized/odd/trailing relocation blocks, out-of-range
or short/unterminated import directory, malformed terminator/incomplete descriptor,
out-of-range lookup/IAT, wrapped/high-bit import-name RVA and null inputs. All 41
local ASan/UBSan programs pass after relinking, including all sixteen retained real
executables. No new fixture, test target or graphics claim. CI's existing strict suite
covers these checks; independent CI results are pending publication.

This is a bounded metadata-correctness improvement, not a complete PE validator or
an OS sandbox. It does not establish Windows-loader parity for malformed inputs,
reject every metadata overlap, add a guest execution timeout or fix native-code trust
and process privileges. External destination buffers may already contain partial
mapping on failure, as before. Complete object lifetime and thread-safety work remains.
PE layout reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

## Batch 75: validate executable launch entry location

run_pe now refuses an entry RVA of zero, an RVA outside SizeOfImage, or an RVA not
inside a declared executable section extent. This check precedes marking the image
loaded, changing page permissions and running TLS callbacks/entry code. It uses the
actual section extent rather than rounded protection pages, so page-tail padding is
not accepted as entry code. Header entry points are outside this launcher's policy.
No instruction decoding, content or termination check is implied.

Expanded existing test_run_pe checks zero/header/out-of-image/max-RVA entry points,
a full-page section gap, writable data entry, a section without EXECUTE, exact section
end and rounded-page tail. Positive checks cover executable non-writable code and
VirtualSize=0/raw-size fallback. Null launcher input is also rejected. All 41 local
ASan/UBSan programs pass after relinking, including the sixteen retained independent
binaries. No new binary/probe, target or CMake change. CI evidence remains pending.

This is an executable-launch policy, not general Windows PE parity. Zero entry RVAs
can have meaning in other PE contexts; this launcher does not launch those images.
TLS callback executable-section validation is not added by this slice. Arbitrary
native instructions remain host-privileged. Full metadata validation, overlaps,
timeouts, partial mapping on failure, COM ownership and thread safety remain open.
Layout reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

## Batch 76: validate all TLS callback launch locations

The same declared-executable-section policy now applies to every TLS callback address
before any callback or entry instruction runs. The entire callback list is validated
up front. A valid early callback cannot execute before an invalid later list member is
found. The validated list is also the list copied into thread attach/detach handling.
The mapper still checks callback pointers are in-image; the runner adds launch policy.

Existing test_tls now rejects header/data/exact-section-end/rounded-page-tail callback
locations. A two-member list begins with a valid-address UD2 callback and ends with a
data pointer: the run is rejected before execution, proving whole-list prevalidation.
A positive check trims the code section to exactly contain the callback's instructions.
After relinking, all 41 local ASan/UBSan programs pass, including sixteen retained
independent binaries and existing TLS/thread fixtures. No new fixture/target/CMake
change or graphics claim. Exact-tip CI evidence remains pending publication.

This checks callback start addresses only, not instruction spans, runtime self-changes,
function signatures or termination. A guest can still execute arbitrary host-privileged
native instructions. General metadata validation, overlaps, partial mapping on failure,
timeouts, COM ownership/lifetime and thread safety remain incomplete. No sandbox claim.
PE TLS layout reference: https://learn.microsoft.com/en-us/windows/win32/debug/pe-format

## Batch 77: strict actual launcher argument parsing

run_exe now rejects unknown flags, extra executable paths/guest arguments, empty paths,
missing/empty value options and duplicate asset-root/save-frame options before opening
a file or launching code. Previously these could silently replace the executable path
or overwrite an earlier value. Boolean flags remain repeatable. Explicit `--` ends
option parsing so a dash-leading executable can be named. An option value beginning
with two dashes must instead be expressed with a path prefix such as `./`.

The existing run_pe test checks the shared parser's valid and invalid forms. A new
shell regression calls the real freshly built CLI seven invalid ways, requires exit2
and usage with no loaded-result line, then successfully runs the retained hello CRT
binary using explicit `--`. CI runs this script strictly and uploads the success log.
All 41 local ASan/UBSan programs and the actual CLI script pass. No new guest variant,
production source compilation unit, test target or CMake addition.

Guest argument forwarding is still absent: a second positional argument is an error,
not an implicit guest argument. This parser does not validate program instructions or
supply OS isolation, timeout or host privilege limits. The existing loader, COM
ownership/lifetime and thread-safety limitations remain. No new graphics claim.

## Batch 78: honor explicit GPU and frame-output requests

Explicit `--gpu` now refuses launch with exit1 if Vulkan initialization or device
selection fails, rather than silently using a reference renderer that ignores shaders.
Omitting `--gpu` still intentionally selects the reference renderer. This checks
backend initialization, not whether a particular draw/render later succeeds.
`--save-frame` now returns exit1 when no valid frame exists or PNG output fails,
even if the guest itself exited0. The output stream is flushed and closed before
success is reported, so buffered-write/close errors are not silently missed.

New actual-CLI shell checks force driver unavailability via an invalid driver manifest,
require failure before the loaded-result line, check absent-frame and missing-directory
output failures, then require successful GPU initialization and a saved triangle PNG.
CI runs these checks strictly and retains the logs/PNG. Local script and previous
CLI parsing script pass, as do all 41 sanitizer test programs. Inspected actual local
320x240 PNG: centered red/green/blue triangle on dark blue background, complete and
readable. This reuses the retained triangle guest, not a new scene or binary variant.
No production compilation unit, test target or CMake addition.

GPU evidence remains software Vulkan only. A saved rendered image does not prove
window screenshot parity or physical graphics/input. This is output/exit-status truth,
not full renderer correctness, atomic file replacement or crash-safe storage. CLI PNG
writes still overwrite a supplied destination and are not confined by the guest asset
root. Native execution and the existing loader/COM lifetime/thread-safety limits remain.

## Batch 79: honor an explicit window request

The CLI wraps its real SDL host to count attempted/successful/failed window opens.
Explicit `--window` now returns failure if SDL cannot load, no guest window ever
opens, or any requested host open fails. Final output reports the counts and whether
the request was satisfied. A loaded guest may still run before the display failure
is known; this is truthful final status, not a preflight display guarantee. Shared
headless behavior outside the CLI remains unchanged.

The actual CLI script forces an invalid SDL video driver, checks a non-windowed hello
guest cannot satisfy the request, then requires a real SDL window open under Xvfb
with the existing triangle guest and Vulkan. All three actual CLI scripts and all41
local sanitizer test programs pass. CI strictly runs the window script under Xvfb and
retains its three logs. No new guest variant, target or CMake addition.

Successful open is not proof of every present, visible window pixels, screenshots,
physical display/input or uncontrolled focus. Failed opens can be retried by guest
calls, but any failure still makes this CLI request fail. Native host privileges,
thread safety, object lifetime and the other earlier limitations remain unchanged.

## Batch 80: query existing resource descriptors

Supported buffers and color/depth Texture2D objects now implement public GetDesc at
resource slot10. They retain creation fields and report resolved mip counts when
MipLevels=0, plus realized single array/sample counts. Depth resources also retain
usage/bind fields used by the existing copy/map policy. Swapchain backbuffers report
actual selected dimensions/format, one mip/sample/layer, default usage and bind flags
mapped from shader-input/render-target buffer usage. Resource and generic-child
vtables remain separate. This adds metadata lookup, not new rendering features.

The existing interface unit checks buffer/color/depth/staging/full-mip/backbuffer
fields and output extents. The existing public-header resource probe is extended with
GetDesc checks for buffer/color/depth/staging and resolved full mip chains. CI requires
the new marker for the fresh probe. The retained run212 probe deliberately remains
unchanged until a newly compiled independent artifact is verified. All41 local
ASan/UBSan programs pass after full relink, including sixteen retained binaries.
Independent new public-header compile/run remains pending CI. No new target/CMake.

Creation validation remains partial: echoing an accepted descriptor field does not
mean its associated feature is implemented. Unsupported arrays/multisampling are
still unavailable, and previously accepted zero array/sample counts are reported
as the realized single resource. No descriptor/private-data support for views/state
objects is added. Existing lifetime/device-retention/ownership/thread-safety and
native-host-privilege limits remain. No new graphics or physical-device claim.
References:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11buffer-getdesc
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11texture2d-getdesc

## Batch81: retain verified independent descriptor regression

Replaces the old resource-interface fixture with exact run230 bytes, not an additional
probe variant. The retained file now establishes its independent public-header GetDesc
checks alongside its earlier interface operations. Fixture-slot requires both markers,
zero exit/no graphics/empty error. CI requires the new descriptor marker in both fresh
and retained paths. All41 local sanitizer programs pass, still sixteen retained guest
binaries. No production/CMake/target change. Accepted descriptor fields still do not
implement unsupported features; all earlier lifetime/ownership/thread-safety limits
remain. No new visual claim.

## Batch82: views retain and return their originating resource

RTV/SRV/DSV objects now retain a COM reference to their originating resource and drop
it when the view reaches zero references. GetResource at view slot7 returns that exact
resource object with an added reference. A separate view vtable avoids resource GetType
slot7 collision. QueryInterface recognizes ID3D11View and each supported concrete view
ID in addition to DeviceChild/IUnknown. No view descriptor method is added.

Existing interface unit tests create each view kind, release the original caller's
resource reference, obtain it through GetResource, release the view, then safely use
and release the returned reference. Exact reference counts are checked. The existing
public-header probe is extended with view/base/concrete identity and GetResource
lifetime checks; CI requires its new marker on the fresh path. Retained run230 bytes
remain unchanged and do not prove these new calls. All41 local sanitizer programs pass
after relinking, including sixteen independent retained binaries. Fresh independent
compiler/run evidence awaits CI. No new target/CMake/binary variant or visual claim.

This fixes the view-to-resource reference edge, not full COM lifetime. Bound views,
shaders and state objects still lack complete binding ownership; children do not retain
their device, and underlying model allocations are not reclaimed by COM release.
Thread safety, full validation and host isolation remain incomplete. Reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11view-getresource

## Batch83: retain verified independent view regression

Replaces the same retained resource probe with exact run239 bytes, source b0b4965,
not an additional fixture. Local independent execution verifies all descriptor/view/
interface markers, zero exit, no graphics and empty error. CI now requires the view
marker in both fresh and retained runs. All41 local sanitizer programs pass and the
retained fixture count remains sixteen. No production/CMake/new-target/visual change.
View-to-resource retention is covered, but binding/device lifetime, model allocation
reclamation, thread safety and native isolation remain incomplete.

## Batch84: retain bound vertex/pixel shaders

VSSetShader/PSSetShader now add a context-owned reference before replacing the old
binding, then release the old reference. Same-object rebinding is safe. Null unbind
and runtime teardown release the held reference. This removes a direct freed-object
read when an application releases its caller reference after binding and then draws.

Existing interface unit checks exact reference deltas for initial bind, same-object
rebind, replacement, unbind and runtime teardown. Teardown testing saves the static
Release function before destroying the runtime, because its per-runtime vtable memory
is not valid afterward. This is test-only reference accounting, not supported object
use beyond runtime lifetime. Existing hand-built PE draw test now releases both caller
shader references before Draw and still passes reference/Vulkan comparison. Existing
public-header probe adds shader binding/reference checks with the existing hand-built
triangle shader data; fresh CI requires the new marker. Retained run239 remains old
view/descriptor regression only. All41 sanitizer programs pass after full relink.
No new scene, guest fixture slot, test target or CMake addition. Fresh independent
compiler/run evidence awaits CI.

This fixes shader binding ownership only. Other state/resource bindings, child/device
lifetime, model allocation reclamation, thread safety and host isolation remain
incomplete. Dynamic class instances are still ignored. No full COM or physical-device
claim. References:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetshader
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshader

## Batch85: retain verified independent shader-binding regression

Replaces the same retained resource probe with exact run248 bytes from source
55e3fa0. Sixteen fixture executables remain. Fixture-slot checks exact output,
zero exit, two translated shaders, no presents and empty D3D11 error. CI requires
the shader-binding completion marker in both fresh and retained paths. All 41
local sanitizer test programs pass. No production code, CMake target, new scene
or release build is included in this retention step.

The independent probe covers caller release, same-object rebind and unbind.
Replacement and runtime teardown accounting remain covered by the interface unit;
the retained probe does not newly establish them. Runtime lifetime is required
for guest interface calls. Other binding/device ownership, model reclamation,
thread safety and native host isolation remain incomplete. No physical-device,
physical-input or Xbox-game compatibility claim follows from this step.

## Batch86: retain supported output-merger views

OMSetRenderTargets now retains its supported single RTV and optional DSV. Both
new references are acquired before either old reference is released, so same-object
rebinding is safe. Replacement, null unbind and runtime teardown release held
references. Counts above one and a missing array with a nonzero count are explicitly
refused before changing bindings, rather than silently selecting the first view.

Existing interface unit checks exact reference deltas for bind/rebind/replacement,
caller release, unbind and teardown, plus unchanged bindings on MRT refusal.
Teardown accounting calls a saved static Release function only. Runtime-owned
vtables remain invalid after runtime destruction; this is not permission for guest
calls after that point. Existing hand-built guest draw now releases the caller RTV
and texture references before Draw; reference and software-Vulkan paths compare
4626 covered pixels with zero mismatches. All 41 local sanitizer programs pass
after full relink. This is automated pixel evidence, not physical-device evidence.

The existing public-header probe adds the caller-release/rebind/unbind checks and
fresh CI requires the output-merger marker. Independent MinGW compilation and
execution await CI. Retained run248 bytes remain shader/view regression only.
No new scene, fixture slot, CMake target or release build is included.

This settles only the supported RTV/DSV binding ownership edge. Multiple render
targets, read/write hazard unbinding, other state/resource binding ownership,
child/device lifetime, model allocation reclamation, thread safety and native host
isolation remain incomplete. Allocation retention previously masked this missing
COM ownership; this fix does not reclaim those allocations. Reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetrendertargets

## Batch87: retain verified independent output-merger regression

Replaces the same retained resource probe with exact run257 bytes from source
15f73fa. Sixteen fixture executables remain. Current-runtime import check covers
52/52 imports and direct execution passes all five markers, zero exit, two shaders,
zero presents and empty error. Fixture-slot checks exact output and CI now requires
the output-merger marker in fresh and retained paths. All 41 local sanitizer
programs pass. No production/CMake/new-target/scene/release change.

Retained independent output-merger checks cover caller release, same-object rebind
and unbind for the single RTV/optional DSV path. Replacement, teardown and MRT
refusal remain interface-unit coverage. Other bindings, child/device lifetime,
model allocation reclamation, thread safety, hazard unbinding and host isolation
remain incomplete. Runtime lifetime remains required for guest interface calls.
No physical-device/input or Xbox-game compatibility evidence is added.

## Batch88: retain pixel shader resource view bindings

PSSetShaderResources retains SRVs per supported slot t0..t3. The incoming batch
is copied and retained in full before old references are released, making cross-slot
swaps safe even when only the context owns those views. Partial updates leave other
slots alone. Null input arrays unbind the requested slots. Runtime teardown releases
each held slot reference. An out-of-range request is refused before any binding
change, with overflow-safe range checks; full 128-slot D3D11 support is not claimed.

Existing interface unit checks caller release, cross-slot swap/rebind, partial and
high-slot replacement, repeated bindings, range refusal, unbind and teardown reference
counts. Teardown uses saved static Release only; guest calls through runtime-owned
vtables still require runtime lifetime. Existing guest textured draw now releases
caller SRV and texture references after binding before each draw. Reference execution
draws four triangles; software Vulkan verifies all eight texture quadrants. All 41
local sanitizer programs pass after full relink. No physical-device/input claim.

The existing public-header probe adds caller-release/cross-slot-swap/rebind/unbind
checks and fresh CI requires its marker. Independent MinGW compilation/execution
awaits CI. Retained run257 remains earlier regression only. No new fixture, scene,
CMake target or release build. This covers only supported pixel SRV ownership.
Other binding ownership, child/device lifetime, model allocation reclamation, thread
safety, output/input hazard auto-unbinding and host isolation remain incomplete.
Reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshaderresources

## Batch89: retain verified independent pixel-SRV regression

Replaces the same resource probe with exact run266 bytes from source 9896e4a.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports and
direct execution passes all six markers, zero exit, two translated shaders,
zero presents and empty error. Fixture-slot requires exact combined output and
retained CI requires the pixel-SRV marker. All 41 local sanitizer programs pass.
No production/CMake/new-target/scene/release change.

Retained independent pixel-SRV checks cover caller release, cross-slot swap,
rebind and unbind. Partial replacement, range refusal and teardown remain unit
evidence. Supported slots are t0..t3, not the full API. Other binding ownership,
child/device lifetime, model allocation reclamation, thread safety, hazard
handling and native host isolation remain incomplete. Runtime lifetime is still
required. No physical-device/input or Xbox-game compatibility evidence is added.

## Batch90: retain supported input-assembler buffers

The sixteen supported vertex-buffer slots and single index binding now retain
COM buffer references. Vertex batches acquire all incoming references before
releasing old slots, preserving solely-context-owned cross-slot swaps. Partial
updates preserve untouched slots. Index rebinding acquires the next reference
before releasing the old one. Null binding, replacement and runtime teardown
release held references. Out-of-range vertex batches refuse before any change;
invalid non-null index formats continue to refuse without replacing the binding.
Vertex strides/offsets remain required for nonempty batches; full optional-array,
stream-output hazard and 32-slot semantics are not claimed.

Existing interface unit checks exact caller-release/swap/rebind/partial-update/
high-slot/index-replacement/unbind/refusal/teardown counts, including the same
buffer held by multiple vertex slots and the index slot at teardown. Teardown
uses saved static Release only, not calls through invalid runtime-owned vtables.
Existing textured guest now releases caller vertex/index buffer references after
binding before indexed draws. Reference execution draws four triangles and
software Vulkan verifies eight texture quadrants. All 41 local sanitizer programs
pass after full relink. No physical-device/input or Xbox-game evidence is added.

The existing public-header probe adds vertex caller-release/swap/rebind and index
retain/unbind checks; fresh CI requires its marker. Independent MinGW compilation
and execution await CI. Retained run266 remains prior regression only. No new
fixture/scene/CMake target/release build. Other bindings, device lifetime, model
allocation reclamation, thread safety and native host isolation remain incomplete.
Runtime lifetime is still required. References:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetvertexbuffers
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetindexbuffer

## Batch91: retain verified independent input-buffer regression

Replaces the same resource probe with exact run275 bytes from source 9bdbde5.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports and
direct execution passes all seven markers, zero exit, two translated shaders,
zero presents and empty error. Fixture-slot requires exact combined output and
retained CI requires the input-buffer marker. All 41 local sanitizer programs pass.
No production/CMake/new-target/scene/release change.

Retained independent input-buffer checks cover vertex caller release/cross-slot
swap/rebind and index retain/unbind. Partial/index replacement, refusal and
teardown remain unit evidence. Sixteen vertex slots and one index binding only;
optional-array/full-slot/hazard semantics remain limited. Other binding ownership,
child/device lifetime, reclamation, thread safety and host isolation remain
incomplete. Runtime lifetime required. No physical-device/input/game evidence added.

## Batch92: retain VS/PS constant-buffer bindings

Both shader stages now hold references for each of their fourteen constant-buffer
slots. The whole incoming batch is acquired before replacing old bindings, so
cross-slot swaps of solely-context-owned buffers are safe. Partial updates preserve
other slots. Null arrays unbind the requested range. Out-of-range batches refuse
atomically with overflow-safe checks. Runtime teardown releases every held slot.

Existing interface unit checks VS and PS caller release, swaps, same-object rebind,
partial and high-slot replacement, invalid ranges, null unbind and teardown.
One shared buffer is bound twice in each stage during teardown to verify all four
held references are dropped. The teardown check uses a saved static Release only;
runtime-owned vtables still require runtime lifetime. All 41 local sanitizer
programs pass after full relink. No new draw/pixel/physical-device claim is made.

The existing public-header probe adds caller-release/swap/rebind/unbind checks for
both stages; fresh CI requires the new marker. Independent MinGW compilation and
execution await CI. Retained run275 remains previous regression only. No new scene,
fixture, CMake target or release build. This fixes only these binding references,
not full buffer validation, D3D11.1 offset binding, other state-object ownership,
child/device lifetime, model reclamation, thread safety or native host isolation.
References:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetconstantbuffers
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetconstantbuffers

## Batch93: retain verified independent constant-buffer regression

Replaces the same resource probe with exact run284 bytes from source d64f396.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports;
direct execution passes all eight markers, zero exit, two translated shaders,
zero presents and empty error. Fixture-slot requires exact combined output and
retained CI requires the constant-buffer marker. All 41 local sanitizer programs
pass. No production/CMake/new-target/scene/release change.

Retained independent constant-buffer checks cover VS/PS caller release, cross-slot
swap, rebind and unbind. Partial replacement/range refusal/teardown remain unit
evidence. Fourteen slots per stage, binding references only; full validation and
D3D11.1 offset binding remain incomplete. Other state/device lifetime, reclamation,
thread safety and host isolation remain incomplete. Runtime lifetime required.
No new rendering, physical-device/input or Xbox-game evidence is added.

## Batch94: retain supported pixel-sampler bindings

The supported s0..s3 sampler slots now hold sampler interface references. Incoming
batches acquire all references before replacing old slots so solely-context-owned
cross-slot swaps are safe. Partial updates preserve other slots; null arrays unbind
the requested range. Out-of-range batches refuse before changes with overflow-safe
checks. Runtime teardown releases each held sampler slot.

Existing interface unit checks exact caller-release/swap/rebind/partial/high-slot/
replacement/range-refusal/unbind/teardown counts, including one sampler bound in
two slots during teardown. Saved static Release only is used after teardown, not
calls through runtime-owned vtables. Existing textured guest releases the caller
sampler reference after binding; reference execution draws four triangles and
software Vulkan verifies all eight texture quadrants. All 41 local sanitizer
programs pass after full relink. No physical-device/input evidence is added.

The existing public-header probe adds sampler caller-release/swap/rebind/unbind
checks; fresh CI requires its marker. Independent MinGW compilation/execution
awaits CI. Retained run284 stays previous regression only. No new fixture/scene/
CMake target/release. This is s0..s3 ownership, not full sixteen-sampler support,
full descriptor validation or corrected null-sampler filtering defaults. Other
state bindings, child/device lifetime, reclamation, thread safety and host isolation
remain incomplete. Runtime lifetime required. Reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetsamplers

## Batch95: retain verified independent pixel-sampler regression

Replaces the same resource probe with exact run293 bytes from source 064c39b.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports;
direct execution passes all nine markers, zero exit, two translated shaders,
zero presents and empty error. Fixture-slot requires exact output and retained
CI requires the sampler marker. All 41 local sanitizer programs pass.
No production/CMake/new-target/scene/release change.

Independent sampler checks cover caller release, cross-slot swap, rebind and
unbind. Partial replacement/range refusal/teardown remain unit evidence. Four
sampler slots/reference ownership only, not full capacity, descriptor validation
or null filtering defaults. Other state/device lifetime, model reclamation,
thread safety and native host isolation remain incomplete. Runtime lifetime is
required. No new physical-device/input or Xbox-game compatibility evidence.

## Batch96: retain copied-state interface bindings

Input layout, rasterizer, depth-stencil and blend setters now retain their bound
interfaces in addition to copying supported state into the model. Each setter
acquires the next reference before dropping the old one, preserving same-object
rebind after caller release. Replacement, null unbind and runtime teardown drop
the held references. Supported state/default rendering behavior is unchanged.

Existing interface unit checks exact reference counts for initial bind, rebind,
replacement, caller release, unbind and runtime teardown for all four kinds.
Teardown uses saved static Release only; runtime-owned vtables still require the
runtime to live. Existing state guest releases all four caller references after
binding before draws/Present. Existing reference depth/scissor/viewport checks and
software-Vulkan comparison pass with zero mismatches. All 41 local sanitizer
programs pass after full relink. No physical-device/input evidence is added.

Existing public-header probe adds caller-release/rebind/unbind for these four
bindings; fresh CI requires its marker. Independent MinGW compilation/execution
awaits CI. Retained run293 stays earlier regression only. No new fixture, scene,
CMake target or release. This fixes these reference edges, not full COM lifetime,
child/device retention, model reclamation, thread safety or host isolation. Ignored
blend factors/sample masks/stencil-reference parameters remain incomplete.
References:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-iasetinputlayout
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-rssetstate
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetdepthstencilstate
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-omsetblendstate

## Batch97: retain verified independent copied-state regression

Replaces the same resource probe with exact run301 bytes from source 802731c.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports;
direct execution passes all ten markers, zero exit, two translated shaders,
zero presents and empty error. Fixture-slot requires exact output and retained
CI requires the copied-state marker. All 41 local sanitizer programs pass.
No production/CMake/new-target/scene/release change.

Independent layout/rasterizer/depth/blend checks cover caller release, rebind and
unbind. Replacement/teardown remain unit evidence. Reference ownership only,
not ignored blend-factor/sample-mask/stencil-reference parameters, full COM/device
lifetime, reclamation, thread safety or native host isolation. Runtime lifetime
required. No physical-device/input or Xbox-game compatibility evidence is added.

## Batch98: ClearState for the supported pipeline subset

Context slot110 now implements ClearState. It drops all tracked shader/view/
input-buffer/constant-buffer/sampler/copied-state binding references, nulling each
tracked pointer before Release. Teardown uses the same release helper. The model
resets supported layouts/buffers/views/constants/samplers, shader layouts/texture
pairing, topology, viewport/scissor and fixed state. Backend and accumulated draw
stats are preserved. ClearState sampler reset uses linear mip filtering/clamp and
unbounded finite LOD; this does not fix the separate null-sampler setter defaults.
Unsupported stages/predication/command lists remain unsupported.

Existing interface unit binds all supported reference categories, including shared
buffer slots and both depth/color views, then clears twice and verifies only caller
references remain. Existing model slot test verifies repeat safety, preserved draw
stats, undefined topology, no render target and missing viewport after reset. All
41 sanitizer programs pass after full relink. These tests do not independently
prove every default's rendered effect or backend reuse after reset.

The existing public-header probe adds shared VS/PS constant-buffer release plus
repeated-clear checks; fresh CI requires its marker. Independent MinGW compilation
and execution await CI. Retained run301 remains earlier regression only. No new
fixture/scene/CMake target/release. This is a supported-subset reset, not full
D3D11 state/lifetime/validation, model reclamation, thread safety or host isolation.
Runtime lifetime still required. No physical-device/input/game claim. Reference:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearstate

## Batch99: retain verified independent ClearState regression

Replaces the same resource probe with exact run313 bytes from source d437ba3.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports;
direct execution passes all eleven markers, zero exit, two translated shaders,
zero presents and empty error. Fixture-slot requires exact output and retained
CI requires ClearState marker. All 41 local sanitizer programs pass.
No production/CMake/new-target/scene/release change.

Independent ClearState evidence is repeated reset/shared VS/PS buffer release.
Other binding-category release remains unit evidence; rendered defaults/backend
reuse are not established. Supported subset only, not full D3D11 state, lifetime,
validation, reclamation, thread safety or native isolation. Runtime lifetime still
required. No physical-device/input or Xbox-game compatibility evidence is added.

## Batch100: query supported bound shaders

VSGetShader(slot76) and PSGetShader(slot74), cross-checked against public header
vtable order, return the exact tracked shader with an added reference. Optional
shader output does not acquire a reference when absent. Class count returns zero;
dynamic class instances remain unsupported and no instance-array entries are
written. No class-instance ownership or broad COM interface support is implied.

Existing interface unit checks empty state, identity after caller release, exact
returned-reference accounting across unbind, optional outputs and NULL after
ClearState. All 41 local sanitizer programs pass after full relink. The existing
public-header probe adds identity/returned-reference checks across ClearState and
fresh CI requires its marker. Fresh probe now translates four shaders, while the
unchanged retained run313 still translates two and lacks the getter marker.
Independent MinGW compilation/execution awaits CI. No new fixture/scene/CMake
or release build and no rendering claim.

This adds two supported-subset query methods, not dynamic classes, complete
state saving/restoring, device lifetime, reclamation, thread safety or host
isolation. Runtime lifetime still required. References:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vsgetshader
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-psgetshader

## Batch101: retain verified independent shader-getter regression

Replaces the same resource probe with exact run322 bytes from source cac602b.
Sixteen fixtures remain. Current-runtime import check covers all 52 imports;
direct execution passes twelve markers, zero exit, four translated shaders,
zero presents and empty error. Fixture-slot requires exact output/four shaders;
retained CI requires getter marker. All 41 local sanitizer programs pass.
No production/CMake/new-target/scene/release change.

Independent shader-getter scope is VS/PS identity, added returned references
after caller release and NULL after ClearState. Unbind/optional outputs remain
unit evidence. Two no-dynamic-class query methods, not full state saving, rendering,
COM/device lifetime, reclamation, thread safety or native host isolation.
Runtime lifetime still required. No physical-device/input/game claim.

## Batch102: refuse unsupported draws with missing shader stages

Draw and DrawIndexed now refuse before model/backend dispatch unless both a vertex
and pixel shader are bound. Previously skipping shader callbacks left the backend's
last shader pair active and could draw with stale shaders after unbind. This is a
fail-closed limitation of this runtime's supported draw path, not complete D3D11
semantics: valid depth-only draws without a pixel shader remain unsupported.

Existing guest draw test first executes its successful guest on reference/software
Vulkan, then uses the same context and tracked shaders to unbind each stage in
turn, call both draw methods, verify the explicit refusal, unchanged target bytes
and unchanged draw stats, and restore the stage. This is host-driven COM regression
after a guest draw, not new independently compiled guest evidence. All 41 local
sanitizer programs pass after full relink. Exact-tip CI evidence awaits publication.
No new scene, fixture, public-header marker, CMake target or release. Retained
run322 remains byte-exact earlier query regression. No rendering capability,
physical-device/input/game or broader lifetime/isolation claim is added.
Reference for NULL disabling the vertex stage:
https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-vssetshader
