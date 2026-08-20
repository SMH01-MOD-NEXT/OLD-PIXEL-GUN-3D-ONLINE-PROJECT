# Third-party notices

## ShadowHook

This project builds the `shadowhook` ARM inline-hook engine from source and
statically links it into the single `libopg3d.so` artifact:

- Upstream: https://github.com/bytedance/android-inline-hook
- Pinned tag: `v2.0.1`
- License: MIT License
- Upstream license text: https://github.com/bytedance/android-inline-hook/blob/v2.0.1/LICENSE

ShadowHook is used only for inline-hooking at an already-resolved absolute
address (`shadowhook_hook_func_addr`); symbol resolution is done by this
project's own ELF dynsym parser, and the upstream CMake targets are not used.
Two local build-time patches are applied (see
`opg3d/src/main/cpp/cmake/patch-shadowhook.cmake`):

- `common/bytesig.c` — the `SA_EXPOSE_TAGBITS` flag is removed from the
  signal handler (armeabi-v7a kernels reject it with EINVAL, which otherwise
  makes `shadowhook_init()` fail with INIT_SIGSEGV);
- `shadowhook.c` — the linker module (pending symbol hooks, dl_init/dl_fini
  callbacks) is disabled, because this project ships a single `.so` and does
  not provide `libshadowhook_nothing.so`, which that module requires.

ShadowHook bundles third-party components under their own permissive licenses:
BSD `queue.h`/`tree.h`, BSD-3-Clause `linux-syscall-support`, and MIT `xDL`.
See the upstream repository for their license texts.

MIT/BSD are compatible with distribution of the combined work under GPLv3;
the ShadowHook component remains available under its own license.
