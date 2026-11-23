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
