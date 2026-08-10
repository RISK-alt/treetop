<!--
Thanks for the patch. CONTRIBUTING.md covers the style rules and the
architecture constraints; this is just the part that is specific to your
change. Delete any section that does not apply.
-->

## What this changes, and why

<!-- The problem first, then the fix. If there is an issue, link it. -->

## How you verified it

<!--
"Tests pass" is enough for a change inside treetop_core, because the tests
reach that code.

It is not enough for anything under src/platform, because no test reaches
that code. Run it against a live machine and paste what it said:

    .\build\Release\treetop.exe --selftest

If the change affects what the screen looks like, describe what you saw.
A rendering change cannot be reviewed from the diff.
-->

## Checklist

- [ ] Builds clean on at least one toolchain, warnings included
- [ ] `ctest` passes
- [ ] New tests, if any, were watched failing before they passed
- [ ] `--selftest` run on a live machine, if this touches `src/platform`
- [ ] No Win32 added to `treetop_core`
- [ ] Nothing keys off a pid without its `create_time`
