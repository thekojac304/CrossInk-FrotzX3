# FrotzX3 contiguous-memory guard test

Host test for `FrotzX3MemGuard.h`, the preflight `init_memory()` runs before it allocates a
story's dynamic memory (policy: `tools/installer/MEMORY_BUDGET.md`). It covers the fit decision
(sufficient, insufficient, exact boundary, no unsigned wraparound, the known story corpus at the
measured v1.6.1 budget) and the user-facing error string. It has no dependencies and is compiled
both as C (as `fastmem.c` uses it) and as C++.

With a local GCC:

```bash
H=tools/patches/v0.9.0-beta.2-crossink-1.6.1/files/lib/FrotzX3/src
gcc -std=c99 -Wall -Wextra -pedantic -Werror -I$H tools/tests/frotzx3_mem_guard/FrotzX3MemGuardTest.c -o mg_c && ./mg_c
g++ -x c++ -std=c++20 -Wall -Wextra -pedantic -Werror -I$H tools/tests/frotzx3_mem_guard/FrotzX3MemGuardTest.c -o mg_cpp && ./mg_cpp
```

On Windows without a host compiler, the same commands run in the `gcc:13` Docker image:

```bash
docker run --rm -v "$PWD":/src -w /src gcc:13 sh -c 'H=tools/patches/v0.9.0-beta.2-crossink-1.6.1/files/lib/FrotzX3/src; gcc -std=c99 -Wall -Wextra -pedantic -Werror -I$H tools/tests/frotzx3_mem_guard/FrotzX3MemGuardTest.c -o /tmp/mg_c && /tmp/mg_c && g++ -x c++ -std=c++20 -Wall -Wextra -pedantic -Werror -I$H tools/tests/frotzx3_mem_guard/FrotzX3MemGuardTest.c -o /tmp/mg_cpp && /tmp/mg_cpp'
```

The allocator call itself and the "no `malloc()` on failure" ordering are checked on hardware with
the forced-failure build described in `tools/installer/MEMORY_BUDGET.md`.
