# Third-party notices

## Dobby

This project statically links the `dobby_static` target from Dobby:

- Upstream: https://github.com/jmpews/Dobby
- Pinned commit: `5dfc8546954ce3b3198132ab13fddb89ee92cdd7`
- License: Apache License 2.0
- Upstream license text: https://github.com/jmpews/Dobby/blob/5dfc8546954ce3b3198132ab13fddb89ee92cdd7/LICENSE

Dobby is used unmodified as the ARM32 inline-hook engine. Its optional symbol
resolver, import-table replacement, Android linker plugin, examples, tests and
debug logging are disabled by this project's CMake configuration.

Apache-2.0 is compatible with distribution of the combined work under GPLv3;
the Dobby component remains available under Apache-2.0.
