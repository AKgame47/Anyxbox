# Reuse from AnyPS5 and PNR

All reused code stays GPL-2.0 and keeps its original credit.

| Area | Source | Plan |
|---|---|---|
| Vulkan backend, swapchain, window | AnythingPS5 | Reuse as is |
| Texture detiling, BC1-BC7 decode, mipmaps | AnyPS5 / AnythingPS5 | Reuse; Xbox swizzle modes added later |
| Shader recompiler structure | AnyPS5 | Keep IR and SPIR-V emitter; new DXBC/DXIL front end |
| Threads, mutex, semaphores, mapped-folder I/O | AnythingPS5 | Reuse, expose through Win32 names |
| Launcher, sandbox, plugins, signed sources | AnythingPS5 (PNR) | Reuse |
| NID function-ID binding | AnyPS5 | Not needed. PE imports use names/ordinals |
| PS5 system-library map | AnyPS5 | Replaced by an Xbox/Win32 import map |

First step on the repo: copy the reusable modules from AKgame47/AnythingPS5 into `src/` with their history noted in commit messages.
