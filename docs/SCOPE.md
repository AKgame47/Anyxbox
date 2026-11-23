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
