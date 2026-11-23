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
