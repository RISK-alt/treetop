# treetop — Design

Date: 2026-08-09
Status: approved, not yet implemented

## 1. What this is

A terminal process monitor for Windows, aimed at developers who work with coding
agents (Claude Code, Cursor, Codex, Aider, and friends).

Task Manager shows fourteen processes named `node.exe`. treetop shows which one
your agent started, what command it is actually running, which port it is
holding, and whether it was left behind when the agent exited.

## 2. Why it exists

Running a coding agent on Windows produces a specific kind of mess:

- A dozen indistinguishable `node.exe` entries with no way to tell them apart.
- Dev servers and file watchers that outlive the session that spawned them, so
  the next `npm run dev` fails on a port that nothing visibly occupies.
- Background builds that pin the CPU with no obvious owner.
- No parent/child view anywhere in the built-in tooling, so "what did this agent
  actually launch?" is unanswerable.

Existing tools each solve part of this. Task Manager has no tree and no command
line. `netstat -ano` gives a PID and nothing else. Process Explorer is excellent
but is a GUI you alt-tab to, not something that lives in the terminal next to
the agent. treetop targets the intersection.

## 3. Goals and non-goals

### Goals

- Live process tree with collapsible nodes and aggregated subtree CPU/memory.
- Full command lines, shortened intelligently, not just image names.
- Listening TCP ports attributed to their owning process.
- Orphan detection with one-key subtree termination.
- Agent-session grouping and an "agents only" view.
- A single self-contained executable, no runtime dependencies.
- `--json` snapshot output so agents can query their own footprint.

### Non-goals (v1)

- Per-process disk and network I/O rates.
- GPU utilisation.
- Temperature sensors.
- Historical graphs.
- Linux or macOS support. The tool is deliberately Windows-only; portability
  would force the abstraction layer to be the product.
- Configuration files. Flags and keybindings only.

## 4. Naming

Binary and project name: `treetop`. A process **tree** viewer that is a `top`
variant. Short, typeable, and it says what it does.

The existing `my-htop` directory name is a placeholder and is replaced.

## 5. Architecture

Five layers. Only the bottom one knows that Windows exists.

```
  win_system  win_process  win_cmdline  win_ports  win_console
  ─────────────────────── platform ──────────────────────────
                          │ raw snapshot
  table → delta → tree → agent detect
  ──────────────────────── model (portable C) ────────────────
                          │ frame model
  meters   table   footer   format   json
  ──────────────────────── render ───────────────────────────
                          │ one UTF-16 buffer
                      win_console
```

The point of the split: the entire core is portable C that consumes an array of
plain structs and produces a frame model. It can be unit-tested against
hand-written snapshots, with no live processes and no Windows API involved.
Tree construction, PID-reuse handling, delta arithmetic and classification are
where the real bugs live, and this is what makes them testable.

### File layout

```
treetop/
├─ .github/workflows/ci.yml
├─ CMakeLists.txt
├─ build.ps1
├─ LICENSE                       MIT
├─ README.md
├─ docs/
│  ├─ design/2026-08-09-treetop-design.md
│  └─ install.md
├─ include/
│  ├─ treetop.h                  app state, shared types
│  ├─ platform.h                 collector interface
│  ├─ process.h                  t_process, table, tree
│  ├─ agent.h                    session and orphan classification
│  ├─ render.h                   frame model
│  ├─ input.h                    keys, filter, sort state
│  └─ theme.h                    palette
├─ src/
│  ├─ main.c                     event loop
│  ├─ platform/
│  │  ├─ win_system.c            per-core CPU, memory, uptime
│  │  ├─ win_process.c           NtQuerySystemInformation + ToolHelp32 fallback
│  │  ├─ win_cmdline.c           command line + owner, cached
│  │  ├─ win_ports.c             GetExtendedTcpTable
│  │  └─ win_console.c           VT setup, alt screen, input, frame write
│  ├─ model/
│  │  ├─ table.c                 snapshot → table keyed by (pid, create_time)
│  │  ├─ delta.c                 CPU percentage between two samples
│  │  ├─ tree.c                  parent linking, orphan detection, flattening
│  │  └─ sort.c                  comparators
│  ├─ agent/
│  │  ├─ detect.c                session root resolution, subtree aggregation
│  │  └─ rules.c                 the pattern table
│  ├─ render/
│  │  ├─ frame.c                 double buffer, single write
│  │  ├─ meters.c                CPU and memory gauges
│  │  ├─ table.c                 process table and columns
│  │  ├─ footer.c                key bar, help overlay
│  │  ├─ format.c                bytes, durations, path shortening
│  │  └─ json.c                  --json serialiser
│  ├─ input/
│  │  ├─ keys.c
│  │  └─ filter.c
│  └─ util/
│     ├─ str.c
│     └─ err.c
└─ tests/
   ├─ harness.h
   ├─ test_tree.c
   ├─ test_delta.c
   ├─ test_format.c
   ├─ test_agent.c
   ├─ test_filter.c
   └─ test_sort.c
```

The current empty `.c` files under `src/cpu`, `src/display`, `src/memory`,
`src/process`, `src/sort`, `src/utils` are removed. The current headers are
rewritten: they model `/proc/stat`, `/proc/meminfo` and `/proc/[pid]/stat`,
none of which exist on Windows.

### Code style

The existing headers follow 42-school conventions: `t_` typedef prefixes,
column-aligned declarations, centred banner comments. Keep that throughout. It
is the house style of this repository and new code should be indistinguishable
from what is already there.

## 6. Data model

```c
typedef struct s_proc_key
{
    unsigned long   pid;
    unsigned long long create_time;   /* FILETIME, disambiguates reused PIDs */
}   t_proc_key;

typedef struct s_process
{
    t_proc_key      key;
    unsigned long   ppid;

    wchar_t         image[64];        /* "node.exe" */
    wchar_t         *cmdline;         /* owned by the cache, may be NULL */
    wchar_t         *user;            /* owner, may be NULL */

    unsigned long long kernel_time;   /* 100 ns units, cumulative */
    unsigned long long user_time;
    unsigned long long working_set;   /* bytes, private if available */
    unsigned long   thread_count;

    double          cpu_pct;          /* computed from previous sample */
    double          mem_pct;

    unsigned short  ports[8];         /* listening TCP ports, 0-terminated */

    int             depth;            /* filled by the tree pass */
    int             is_orphan;
    int             is_agent_root;
    int             collapsed;

    double          subtree_cpu;      /* aggregates, shown when collapsed */
    unsigned long long subtree_mem;
    int             subtree_count;

    struct s_process *parent;
    struct s_process *first_child;
    struct s_process *next_sibling;
}   t_process;
```

Processes live in a flat array owned by the table; tree links are pointers into
that array. Collapsed state and selection survive across refreshes because they
are keyed on `t_proc_key`, not on array index.

## 7. Data acquisition

### Process list

Primary: `NtQuerySystemInformation(SystemProcessInformation)`. One call returns
every process with PPID, kernel/user time, working set, thread count and
creation time. Roughly 2 ms on a 300-process machine.

Fallback: `CreateToolhelp32Snapshot` plus per-process `GetProcessTimes` and
`GetProcessMemoryInfo`. Roughly 80 ms. Used only when the `ntdll` symbol cannot
be resolved. When the fallback is active the header shows a discreet
`limited mode` marker.

In limited mode, creation time comes from `GetProcessTimes`, which needs a
handle. Where the handle cannot be opened, `create_time` is 0 and the process is
keyed on PID alone. Consequences, accepted and documented: such a process can
never be a validated parent (so its children become roots rather than being
mis-attributed), and it is never flagged as an orphan. Degrading toward "no
claim" rather than a wrong claim is the rule everywhere in limited mode.

Both `NtQuerySystemInformation` and `NtQueryInformationProcess` are resolved at
runtime with `GetProcAddress` on `ntdll.dll`. Nothing links against a DDK, so
the project builds with a stock MinGW-w64 or MSVC installation.

### Command lines

Primary: `NtQueryInformationProcess(ProcessCommandLineInformation)`, class 60,
available since Windows 8.1. It needs only `PROCESS_QUERY_LIMITED_INFORMATION`.

Fallback: `ProcessBasicInformation` → read the PEB → `ProcessParameters` →
`CommandLine`, which additionally requires `PROCESS_VM_READ`.

A command line never changes during a process's lifetime, so it is fetched once
and cached under `(pid, create_time)`. After the first tick this costs nothing.
Protected and other-user processes will fail; they keep their image name and
display `—`.

### Ports

`GetExtendedTcpTable` with `TCP_TABLE_OWNER_PID_LISTENER`, plus the IPv6 table.
Heavier than the process query, so refreshed on its own timer — at most once
every 3 seconds, independent of `--refresh`. Tying it to a tick count would mean
a 15-second port lag at `--refresh 5000`.

### System totals

Per-core CPU from `NtQuerySystemInformation(SystemProcessorPerformanceInformation)`,
which returns idle/kernel/user per logical processor. Falls back to
`GetSystemTimes` for a global figure only. Memory from `GlobalMemoryStatusEx`.

### Console

- `SetConsoleMode` with `ENABLE_VIRTUAL_TERMINAL_PROCESSING`; if it fails, print
  an actionable message and exit rather than emit garbage.
- `SetConsoleOutputCP(CP_UTF8)`.
- Alternate screen buffer via `\x1b[?1049h`, restored on exit including on
  `SIGINT`.
- Input via `ReadConsoleInput`, which also delivers `WINDOW_BUFFER_SIZE_EVENT`
  for resize.

## 8. Core algorithms

### Event loop

Wait on the console input handle with a timeout equal to the time remaining
until the next refresh. Keys stay responsive at any refresh rate, and the
refresh cadence does not drift.

### CPU percentage

```
cpu_pct = (Δkernel + Δuser) / (Δwall × n_logical_cores) × 100
```

Times are 100 ns units; wall time comes from `GetSystemTimeAsFileTime`.
Normalised so a fully loaded machine reads 100 %, matching Task Manager rather
than htop's per-core convention — the audience is a Windows audience.

On the first tick there is no previous sample: report 0 %, never a garbage
figure derived from cumulative time since boot.

### Process identity and PID reuse

Windows reuses PIDs aggressively. Everything — the command-line cache, collapsed
state, selection, previous-sample lookup — is keyed on `(pid, create_time)`. A
naive PID-only implementation eventually attributes children to the wrong parent
and reports impossible CPU spikes.

### Tree construction

Link each process to its PPID, then validate: **a parent must be older than its
child**. If the process at `ppid` was created *after* the child, that PID has
been recycled and the real parent is gone. Such a process becomes a root.

### Orphan detection

A process is flagged as an orphan when its real parent is gone *and* it is worth
telling the user about:

```
parent_missing AND (holds_listening_port OR image ∈ dev_runtimes)
```

where `dev_runtimes` is `node, deno, bun, python, pythonw, cargo, rustc,
dotnet, java, go, pwsh, powershell, cmd, npm, pnpm, yarn, vite, webpack`.

The qualifier matters. Windows is full of legitimately re-parented processes —
anything launched from a terminal that has since closed — and flagging all of
them would paint the screen red and make the feature useless. The two conditions
above narrow it to the case the user actually cares about: something a tool left
running.

### Agent session detection

Walk from each root downward. A process becomes a **session root** when its
image name or the first token of its command line matches a rule and no
ancestor already matched. Its whole subtree is then a session, and subtree
CPU/memory/count are aggregated onto the root for display when collapsed.

Rules live in a single table in `src/agent/rules.c`:

```c
static const t_agent_rule g_rules[] = {
    { L"claude",     MATCH_IMAGE | MATCH_CMDLINE, L"Claude Code" },
    { L"cursor",     MATCH_IMAGE,                 L"Cursor"      },
    { L"codex",      MATCH_IMAGE | MATCH_CMDLINE, L"Codex"       },
    { L"aider",      MATCH_IMAGE | MATCH_CMDLINE, L"Aider"       },
    { L"windsurf",   MATCH_IMAGE,                 L"Windsurf"    },
    { L"gemini",     MATCH_CMDLINE,               L"Gemini CLI"  },
    /* ... */
};
```

`MATCH_CMDLINE` matters because several agents run as `node.exe` with the agent
name only in `argv`. One line per agent, so adding one is an obvious and
welcoming first contribution — the README says so explicitly.

### Rendering

The whole frame is composed into a UTF-16 buffer and emitted with a single
`WriteConsoleW`. No partial redraws, no flicker, and no cursor-positioning
bookkeeping. At 1 Hz on a 250-row terminal the cost is irrelevant.

## 9. Interface

### Layout

```
┌─ treetop ─────────────────────────── 14:32:07 ─ 241 proc ─┐

 CPU ▐████████▌░░░░░░░░▌ 47%   ▍0 ▍1 ▍2 ▍3 ▍4 ▍5 ▍6 ▍7
 MEM ▐████████████▌░░░░▌ 61%   19.4 / 32.0 GB

├───────────────────────────────────────────────────────────┤
  PID   CPU%   MEM   PORTS  COMMAND
─────────────────────────────────────────────────────────────
▼ 4821  38.2  2.1G          claude
  ├ 9012 31.0 1.4G   :5173  node vite dev --host
  ├ 3344  4.1  180M         pwsh -c npm test
  └ 7781  0.0   22M         rg --json "useState"
▶ 1190  12.0  1.8G          cursor  (2 children)
! 2210   0.2  410M   :3000  node server.js          ORPHAN
─────────────────────────────────────────────────────────────
 F1 help  F5 tree  F6 sort  F9 kill  / filter  a agents  q
```

Columns adapt to terminal width: below 100 columns the per-core strip collapses
to a single aggregate bar; below 80, the PORTS column merges into COMMAND.

### Keys

| Key | Action |
|---|---|
| `↑` `↓` / `j` `k` | Move selection |
| `←` `→` / `Space` | Collapse / expand |
| `a` | Agents-only view: sessions and orphans, nothing else |
| `o` | Orphans-only view |
| `/` | Incremental filter over image name and command line |
| `Esc` | Clear filter |
| `F5` | Toggle tree / flat |
| `F6`, `<` `>` | Sort column |
| `F9` | Kill selected process, with confirmation |
| `Shift+F9` | Kill entire subtree, confirmation lists what will die |
| `p` | Freeze refresh |
| `+` `-` | Refresh interval |
| `F1` / `?` | Help overlay |
| `q`, `Ctrl+C` | Quit |

`a` is the centrepiece. One keystroke reduces the screen to: your agent
sessions, what they spawned, and what they forgot to clean up.

### Colour

24-bit truecolor via VT sequences, with one governing rule: **colour encodes
load and nothing else**. A single green → yellow → orange → red ramp drives both
the gauges and the `CPU%` column, so peripheral vision alone locates what is
hot.

Everything else is monochrome, ranked by intensity: paths and arguments dim,
command names bright, agent session roots in the accent hue, orphans red with a
`!` in the gutter. The selected row is inverted.

`NO_COLOR` is honoured; `--no-color` exists.

## 10. Command line

```
treetop [options]

  --json              print one snapshot as JSON to stdout and exit
  --refresh <ms>      refresh interval, default 1000
  --no-color          disable colour output
  --selftest          verify the platform collectors and exit
  --version
  --help
```

`--json` is the feature that makes treetop useful to agents and not only to
humans: an agent can query its own footprint, find the ports it is holding and
clean up after itself. The model is already separated from rendering, so this is
one serialiser over the existing frame model.

The JSON shape is documented in the README and treated as a stable interface
from the first tagged release.

## 11. Error handling

Nothing in the program is allowed to abort on a data error.

| Condition | Behaviour |
|---|---|
| Protected process, cannot read command line | Show image name, `—` for command |
| `ntdll` resolution fails | Fall back to ToolHelp32, show `limited mode` |
| Terminal lacks VT support | Clear message with remediation, exit 1 |
| Terminal resized | Reflow on the next frame |
| `TerminateProcess` denied | Status line explains, suggests elevation |
| Process exits between sample and render | Dropped silently on the next tick |

## 12. Testing

No external framework. `tests/harness.h` provides assertion macros in about
thirty lines: zero dependencies and consistent with the rest of the project.

Covered:

- Tree construction, including PID reuse and orphan cases
- CPU delta arithmetic, including the first tick, a zero-length wall interval,
  and cumulative time appearing to move backwards (which happens when a PID is
  reused between two samples and the guard fails)
- Byte and duration formatting
- Path shortening
- Filter matching
- Sort comparators, including stability
- Agent classification rules

The platform layer is not unit-tested — it talks to the live OS. `--selftest`
covers it instead by exercising each collector on the current machine and
reporting what succeeded.

## 13. Build and distribution

**Build**: CMake 3.15+, C11, working with both MinGW-w64 and MSVC. Links
`iphlpapi` and `psapi`; `ntdll` is resolved at runtime. `build.ps1` wraps the
whole thing in one command.

**CI**: GitHub Actions on `windows-latest`, building with both toolchains and
running the test suite. On a `v*` tag, publishes `treetop.exe` to Releases with
its SHA256.

## 14. Install guide

`docs/install.md`, linked prominently from the README, covering five things —
two of which most projects omit and which are exactly where users give up:

1. **Fast path**: download `treetop.exe` from Releases, put it on `PATH`, run.
2. **From source**: prerequisites (MSYS2 *or* Visual Studio Build Tools),
   commands, verification step.
3. **The SmartScreen warning**. An unsigned executable downloaded from GitHub
   triggers "Windows protected your PC". Undocumented, the user deletes the file
   and leaves. Explain what it is, show the SHA256 check, show how to proceed.
4. **Font and encoding**. The block and box-drawing characters need a capable
   monospace font. Without it the screen is a field of replacement glyphs.
   Recommend Cascadia Mono or JetBrains Mono, and Windows Terminal over
   `conhost`.
5. **Troubleshooting**: access denied on system processes and what elevation
   changes; `limited mode`; what to do when `treetop` is not found after
   extraction.

## 15. Known limitations

Stated plainly in the README rather than discovered by users:

- Command lines of protected and other-user processes are unreadable without
  elevation. This is a Windows security boundary, not a bug.
- No per-process disk or network I/O. Getting these reliably requires ETW, which
  is a project of its own.
- UDP listeners are not shown; only TCP.
- Windows 10 1809 or later. Earlier builds lack reliable VT support.
- Agent detection is pattern-based. An agent invoked in an unusual way may not
  be recognised — the rule table is one line per entry and takes a pull request.

## 16. Repository presentation

The repository should read as what it is: a tool one developer built because
they were annoyed. Concretely:

- README in the first person, opening with the problem, not a feature list.
  No emoji headings, no "✨ Features" section, no superlatives.
- The comparison table against Task Manager, because the reasonable first
  question is "why not just use Task Manager".
- An honest "Known limitations" section, section 15 above.
- Incremental commits that show the tool being built — platform layer, then
  model, then rendering, then interaction — rather than one enormous initial
  commit.
- MIT licence.
- A screenshot or a short asciinema-style capture at the top of the README. It
  is the single highest-leverage element for a TUI project.
