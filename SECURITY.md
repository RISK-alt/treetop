# Security policy

## Reporting a vulnerability

Report it privately through
[GitHub's advisory form](https://github.com/risk-alt/treetop/security/advisories/new).
Do not open a public issue.

Expect an acknowledgement within a week. This is a project maintained by
one person in their own time, so that is a realistic figure rather than an
ambitious one. If you have had no reply after two weeks, a nudge is
welcome.

## Supported versions

The most recent release. There are no maintained branches for older
versions, and there is no backporting.

## What is worth reporting

treetop reads process information and can terminate processes, so the
interesting cases are around what it reads, what it believes, and what it
acts on:

- **Anything that turns displayed data into executed data.** Command
  lines, image names and window titles come from other processes on the
  machine and are treated strictly as text to draw. A crafted command line
  that escapes the renderer, injects terminal escape sequences into the
  output, or is interpreted rather than displayed, is a real bug.
- **Killing the wrong process.** Process identity here is
  `(pid, create_time)` specifically because Windows reuses PIDs within
  seconds. If you find a path where a stale identity reaches
  `plat_kill()`, that is the most serious class of bug this program has,
  and it is worth reporting even if you can only trigger it by racing.
- **Memory safety in parsing.** Command-line recovery reads the target
  process's PEB, and the port tables come from `GetExtendedTcpTable`.
  Both parse structures this program does not control.

## What is not a vulnerability

- **The SmartScreen warning.** The released binary is unsigned, because
  code-signing certificates cost money. The install guide explains this
  and publishes a SHA256 to check against. It is expected, not a defect.
- **Needing Administrator to see or kill some processes.** treetop runs
  with your privileges and does not attempt to acquire more. Processes
  owned by other users or by the system are shown with what is readable
  and refuse to be killed. That is Windows working correctly.
- **Reading other processes' command lines at all.** This is the point of
  the program, it requires no special privilege for your own processes,
  and Process Explorer and Task Manager do the same.
- **Anything requiring Administrator or physical access to set up.** An
  attacker who already has that does not need treetop.
