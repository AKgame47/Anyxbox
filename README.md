# Anyxbox

Anyxbox is a native compatibility runtime that aims to run Xbox-family x86-64 programs on a normal Windows or Linux PC. It works like Wine: program code runs directly on the PC's CPU, and calls into the console's system software are translated to PC equivalents (Win32/NT subset, Direct3D to Vulkan, audio, input, saves).

There is no CPU translation layer. Xbox One and Xbox Series consoles use x86-64 CPUs and Windows-family system software, so the gap to a PC is smaller than for other consoles. That is the reason this project exists.

Companion to [AnyPS5](https://github.com/boykopovar/AnyPS5) (fork: [AnyPS5-by-AK](https://github.com/AKgame47/AnyPS5-by-AK)) and [AnythingPS5](https://github.com/AKgame47/AnythingPS5). Code is adapted from those projects under GPL-2.0 with credit.

## Status

Phase 0 scaffold. Nothing runs yet. See [docs/ROADMAP.md](docs/ROADMAP.md).

Release builds are produced only after every implementation phase is complete.

## Scope

Anyxbox targets homebrew programs and test binaries you build yourself.

- It does not break Microsoft encryption, signing or DRM.
- It does not include, download or link to game dumps, keys or pirated sources.
- It does not ship any Microsoft code or firmware.

See [docs/SCOPE.md](docs/SCOPE.md).

## Layout

| Path | Purpose |
|---|---|
| `src/loader` | PE/COFF loader, import binding, relocations |
| `src/syslayer` | Win32/NT subset and console system-library mapping |
| `src/gfx` | Direct3D 11/12 to Vulkan translation, DXBC/DXIL to SPIR-V |
| `src/audio_input` | Audio, gamepad, saves |
| `src/launcher` | Library, compatibility labels, sandbox (from PNR) |
| `tests` | Self-built sample programs and unit tests |
| `docs` | Roadmap, scope, AnyPS5 reuse map |

## License

GPL-2.0. See [LICENSE](LICENSE).
