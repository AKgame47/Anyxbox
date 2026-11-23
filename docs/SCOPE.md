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
Not yet supported: relative (dynamically indexed) cbuffer operands and a second constant buffer (Wine "relative addressing in constant buffers" VS fails with a clear error, checked in test_real_dxbc_tex).
