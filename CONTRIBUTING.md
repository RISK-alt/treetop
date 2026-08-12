# Contributing to treetop

Thanks for looking. This file is meant to answer, in one read, the
questions you would otherwise have to open a PR to find out: what belongs
here, how to build it, what the CI will fail you on, and what a change is
expected to prove about itself.

## What belongs here

treetop is a process viewer for Windows, aimed at people whose machines
are running coding agents. That focus is the whole design, so the useful
test for a proposed change is whether it helps someone answer one of
these:

- what did this agent session spawn, and is any of it still running?
- which process is holding the port my dev server wants?
- what got left behind when a session ended?

Things that fit well: better agent detection, better process
identification, more readable command lines, port and socket detail,
anything that makes an orphan easier to recognise or kill.

Things that fit badly: cross-platform ports (the platform layer is the
whole program; see below), general system-monitoring features with no
connection to process trees, and settings for things that could just have
one good default.

If you are unsure, open an issue before writing code. A short description
of the problem you hit is more useful than a patch for a problem nobody
else has.

## Getting set up

You need Windows 10 1809 or later, x64, and CMake 3.15+. Either
toolchain works:

- **MSVC** — install the "Desktop development with C++" workload from the
  Visual Studio Build Tools. Nothing else.
- **MinGW-w64** — the UCRT64 environment from [MSYS2](https://www.msys2.org/),
  with `mingw-w64-ucrt-x86_64-gcc`, `-cmake` and `-ninja`.

There is no SDK or DDK requirement. The NT calls this program makes are
resolved from `ntdll` at runtime with `GetProcAddress` rather than linked,
so nothing has to be installed to satisfy a linker.

Build and test:

```powershell
.\build.ps1 -Test
```

or by hand, which is what CI does:

```powershell
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Then check it against your own machine:

```powershell
.\build\Release\treetop.exe --selftest
```

`--selftest` exercises every platform call against the live system and
exits non-zero if any of them fails. It is the fastest way to find out
whether a platform change works, because the unit tests deliberately
cannot reach that code.

## How the code is laid out

The one structural rule, which everything else follows from:

**`treetop_core` contains no Win32. Ever.**

`src/model`, `src/render`, `src/agent`, `src/input` and `src/util` are
portable C. They are compiled into a static library that the tests link
directly, which is why the tree building, delta arithmetic, sorting,
filtering, agent classification and the entire renderer can be tested
without a Windows process in sight.

Everything that talks to Windows lives in `src/platform` behind the
interface in `include/platform.h`, and is linked only into the executable.
If you find yourself wanting `<windows.h>` in a core file, the design
wants that logic split: a platform function that gathers facts, and a core
function that decides what they mean.

The other invariant worth knowing before you touch the model:

**A process is identified by `(pid, create_time)`, never by pid alone.**

Windows reuses PIDs within seconds, and an agent session churns through
processes fast enough that you will hit it. Every delta, every parent
link, every selection that survives a refresh depends on not mistaking a
new process for the one that just exited. `resolve_parent` in
`src/model/tree.c` will refuse a parent that is younger than its child for
exactly this reason. If you add code that keys off a pid on its own, it
will be wrong on a live machine long before anyone notices in a test.

## Adding support for an agent

This is the most likely reason you are here, and it is deliberately a
small change. `src/agent/rules.c` is one line per agent:

```c
{ L"claude",    MATCH_IMAGE | MATCH_CMDLINE, L"Claude Code" },
```

The needle is lowercase and matched case-insensitively. The flags say
where to look:

- `MATCH_IMAGE` — the executable name. Use this when the agent ships its
  own binary.
- `MATCH_CMDLINE` — the command line. Necessary for anything that runs as
  `node.exe` with its real identity only in argv, which is most of them.
- Both, when either can happen.

Be careful with short needles. Matching is on token boundaries precisely
because a bare substring search is wrong: `amp` used to match
`steamwebhelper.exe`, whose command line contains `-steampid=`, and Steam
was reported as an agent session until that was fixed. If your needle is
short or is a common English fragment, say so in the PR.

**Every rule needs a test, and the test needs a negative case.** Add it to
`tests/test_agent.c` next to the others:

```c
p = mk_proc(1, 1, 0, L"node.exe");
p.cmdline = L"node C:\\Users\\x\\AppData\\npm\\youragent\\cli.js";
TT_CHECK(agent_match(&p) != NULL);

/* The plausible false positive, whatever it is for your needle. */
p = mk_proc(2, 1, 0, L"unrelated.exe");
p.cmdline = L"unrelated --something-youragent-like";
TT_CHECK(agent_match(&p) == NULL);
```

## Tests

The suite is in `tests/`, built into one binary, run by `ctest`. It uses
its own macros from `tests/harness.h` (`TT_CHECK`, `TT_EQ_INT`,
`TT_EQ_WSTR`) rather than `assert`, so that assertions stay live in
Release builds, where `NDEBUG` would otherwise compile them out.

One expectation about new tests, and it is the one most likely to come up
in review:

**A test must be able to fail.** Before you submit it, break the code it
covers and watch it go red. This is not a formality. Ten tests in this
project's history passed against deliberately broken implementations, and
every one of them looked correct when read. A test whose assertion is
satisfied by the bug it is supposed to catch is worse than no test,
because it will be trusted.

If a test covers a guard, delete the guard and confirm the test fails.
Then put the guard back.

## Style

The house style is unusual and the CI enforces the mechanical parts of it,
so read this before your first PR rather than after:

- Four spaces, never tabs.
- 80 columns, hard limit, in `.c` and `.h`.
- No trailing whitespace.
- `#include` and `#define` have **no** space after the `#` in `.c` files.
  The spaced form (`# include`, `# define`) is reserved for headers. Three
  separate changes in this project's history got this wrong, which is why
  a linter now checks it.
- Types are prefixed: `t_` for structs and typedefs, `e_` for enums, `g_`
  for globals.
- One blank line between functions, and a comment above anything whose
  reason is not obvious from its name. Comments explain why, not what.

Match the file you are editing. If in doubt, the surrounding code is the
specification.

## Commits and pull requests

Write commit messages that explain the reasoning, not the diff. The diff
is already in the commit; what it cannot show is what you tried, what you
ruled out, and why this shape rather than another. Subject line in the
imperative, under about 70 characters, optionally prefixed with an area
(`fix:`, `docs:`, `ci:`, `build:`).

For the pull request itself:

- Branch off `main`, one topic per PR.
- Say how you verified the change. "Tests pass" is not verification if the
  change is in `src/platform`, because no test reaches that code; run
  `--selftest` and paste what it said.
- If the change affects what the screen looks like, say what you saw.
  Nobody can review a rendering change from the diff alone.

## What CI will run

Every push and pull request gets:

| Job | What it does |
| --- | --- |
| style lint | The mechanical style rules above |
| build (mingw, Debug and Release) | GCC, warnings as errors |
| build (msvc, Debug and Release) | MSVC `/W4 /WX`, warnings as errors |
| address sanitizer | Unit tests under MSVC ASan |

Both compilers build with warnings as errors, so a warning on either one
fails the build. That is deliberate: the two disagree often enough about
version-gated Windows headers, implicit conversions and unused results
that building on only one of them lets real defects through. This project
already shipped a bug that only MSVC could see.

Tagged pushes (`v*`) additionally run the release workflow, which checks
that the tag matches `TT_VERSION` in `include/treetop.h`, builds, tests,
and refuses to publish a binary that imports any DLL Windows does not
already provide. The README promises one file with no runtime to install,
and that check is what keeps the promise true.

## Reporting bugs

Use the issue templates; they ask for the things that are otherwise a
round-trip. In particular, for anything involving what treetop shows you,
`treetop --json` output is worth more than a screenshot, and it is what
the maintainer will ask for.

If you have found a security issue, do not open an issue. See
[SECURITY.md](SECURITY.md).

## Conduct

[CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) applies to issues, pull requests
and discussions here. It is the Contributor Covenant, unmodified except
for saying where a report goes, and it exists so that the answer to "what
happens if someone behaves badly" is written down before it is needed
rather than improvised afterwards.

## Licence

By contributing, you agree that your contributions are licensed under the
MIT Licence, the same as the rest of the project.
