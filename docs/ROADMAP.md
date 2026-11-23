# Roadmap

Rule: build every phase first. Release builds (Windows .exe, Linux) only after all phases are implemented.

0. Feasibility, scope, reuse map (this scaffold).
1. Loader: PE/COFF parsing, sections, relocations, import tables, TLS, init/fini. Much simpler than the ELF/NID path in AnyPS5.
2. System layer: Win32/NT subset (memory, threads, sync, files in a mapped folder, time). Reuse AnyPS5/PNR threading, mutex, semaphore and mapped-folder I/O work.
3. Graphics: D3D11 first, then D3D12. DXBC/DXIL to SPIR-V replaces the RDNA shader recompiler. Reuse the Vulkan backend, depth/blend/cull, texture formats (BC1-BC7), mipmaps, arrays and cubemaps.
4. Audio, gamepad input, saves.
5. Launcher: library, compatibility labels, install/verify, sandbox, plugin system, adapted from PNR.
6. Compatibility testing on self-built samples, tamper-evident log, CI on Linux and Windows.
7. Hardware-GPU verification, performance tuning, adaptive quality for low-end PCs.
8. Release builds.

See [REUSE.md](REUSE.md).
