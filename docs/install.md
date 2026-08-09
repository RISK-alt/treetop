# Installing treetop

## Fast path

Download the executable and run it. No installer, no runtime, one file.

```powershell
irm https://github.com/risk-alt/treetop/releases/latest/download/treetop.exe -OutFile treetop.exe
Move-Item treetop.exe $env:LOCALAPPDATA\Microsoft\WindowsApps\
treetop
```

`%LOCALAPPDATA%\Microsoft\WindowsApps` is already on `PATH` for every user
account on Windows 10 and 11, so dropping the exe there is enough to make
`treetop` a bare command in any terminal, with no PATH editing. If you'd
rather keep it somewhere else, that's fine too — just run it by its full
path, or add that folder to `PATH` yourself.

Before it runs, Windows will very likely show you a warning. That's
normal and expected for an unsigned executable downloaded from the
internet — see [The SmartScreen warning](#the-smartscreen-warning)
below before you decide what to do about it.

## From source

**Prerequisites** — one of:

- MSYS2 (or any standalone MinGW-w64 distribution) with `gcc`, `cmake`
  and a generator (`ninja` or `mingw32-make`) on `PATH`.
- Visual Studio Build Tools with the "Desktop development with C++"
  workload, which brings its own `cmake`.

Either way you need CMake 3.15 or newer and a C11 compiler. There is no
Windows SDK or DDK dependency — the two `ntdll` functions treetop needs
are resolved at runtime with `GetProcAddress`, never linked, so a stock
compiler install is enough.

**Build:**

```powershell
git clone https://github.com/risk-alt/treetop.git
cd treetop
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Verify:**

```powershell
ctest --test-dir build -C Release --output-on-failure
.\build\treetop.exe --selftest
```

The first command runs the portable test suite (1,810 assertions, no live
process table involved — the whole model and render layer is tested
against hand-written snapshots). The second exercises the actual
platform collectors — process enumeration, command lines, ports, system
metrics — against whatever is really running on your machine right now,
and reports what worked:

```
treetop selftest
  ntdll entry points ............ ok
  process enumeration ........... ok      290 processes
  command lines ................. ok      142/290 readable
  listening ports ............... ok      27 ports on 20 processes
  system metrics ................ ok      16 cores, 31.2 GB
  tree .......................... ok      16 roots, max depth 9
  agent sessions ................ ok      1 session (Claude Code), 1 orphans
```

(The exact numbers on the "agent sessions" line depend on what's actually
running on your machine when you run it — a machine with nothing agentic
running reports 0 sessions, and that's a pass too, not a failure.)

Both should exit 0. `build.ps1` wraps the configure/build/test sequence
above in one command if you'd rather not type all four.

## The SmartScreen warning

The first time you run an executable downloaded from the internet that
isn't signed by a certificate Windows already trusts, you'll see this:

> **Windows protected your PC**
> Microsoft Defender SmartScreen prevented an unrecognized app from
> starting. Running this app might put your PC at risk.

This is not specific to treetop — it's what happens to any unsigned
`.exe` from GitHub Releases, and code-signing certificates cost real
money for a project that costs nothing to use. It is not, by itself, a
reason to distrust the file, but you shouldn't take that on faith either.
Check the hash against the one published alongside the release:

```powershell
Get-FileHash .\treetop.exe -Algorithm SHA256
```

Compare the output against the SHA256 value on the release page for the
version you downloaded. If they match, click **More info** on the
SmartScreen dialog, then **Run anyway**. If they don't match, delete the
file and download it again from the Releases page directly rather than
wherever you got the mismatched copy — don't run it.

## Font and encoding

treetop draws its tree with box-drawing characters (`├`, `└`, `─`) and
its CPU/memory gauges with Unicode block glyphs (`█`, `▓`, `▒`, `░`).
Most bundled Windows fonts don't cover these, so if your terminal is set
to Consolas or a similar default, expect a screen full of empty boxes
instead of a tree.

Use a font that actually covers them — **Cascadia Mono** (ships with
Windows Terminal, or install it separately) or **JetBrains Mono** both
work well. Prefer **Windows Terminal** over the legacy `conhost` window
too: its font handling and Unicode coverage are simply better maintained,
and it's what treetop is developed and screenshotted against. `conhost`
will generally still work, just with a higher chance of the wrong glyphs
turning up for one of the block or box-drawing characters even once the
font itself supports them.

## Troubleshooting

**`treetop: failed to initialise the console` and it exits immediately**
The terminal you're running in doesn't support the VT escape sequences
treetop draws everything with. This shows up on old or unusual terminal
emulators; it should not happen in Windows Terminal or a modern
`conhost` (Windows 10 1809+). Switching to Windows Terminal is the fix.

**"access denied" when killing a process, or command lines showing `—`**
Reading another user's or a protected process's command line, and
terminating processes you don't own, both require privilege treetop
doesn't request by default. Re-run your terminal as Administrator. Some
processes stay off-limits even then — that's the OS enforcing a real
security boundary, not something elevation is meant to bypass entirely.

**The header shows "limited mode"**
treetop resolves two `ntdll` functions at startup for fast, complete
process enumeration. If that fails (rare, but sandboxed or heavily
locked-down environments can block it) it falls back to the slower
ToolHelp32 API and says so in the header rather than pretending nothing
changed. The tool still works in this mode; process listing is just
slower, and orphan detection is a little more conservative because
process creation times aren't reliably available in this fallback path
(see [the design notes](design/2026-08-09-treetop-design.md#7-data-acquisition)
on why that matters for telling parent from child).

**`treetop : The term 'treetop' is not recognized...` after extracting it**
The exe isn't on `PATH`, or your current shell was opened before you
added it and hasn't picked up the change. Either open a new terminal
window, or just run it by its full path (`.\treetop.exe` from the folder
you put it in) to confirm it works before worrying about `PATH` at all.
