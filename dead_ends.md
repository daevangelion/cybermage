# Dead Ends & Investigation Notes

Read before starting a session. Active entries become RESOLVED when understood;
remove obsolete entries after 20+ sessions.

## LE direct-call census is not a code/data classifier
- **Tried:** use `dosre funcs` default entry-point traversal as a function census.
- **Failed because:** indirect dispatch and callback roots are missed (417 functions,
  ~18% of code). Debug-global roots increase coverage to ~71%.
- **Better approach:** seed Watcom code symbols and follow fixup targets; inspect
  remaining ranges before classifying them.
- **Session:** bootstrap.

## Aliased Watcom globals
- **Tried:** one dosre label row per debug global.
- **Failed because:** 1,718 globals occupy 1,677 unique addresses; dosre rejects
  duplicate addresses.
- **Better approach:** one primary label per address, retaining every aliased
  readable/mangled name and source module in its comment. Implemented in generator.
- **Session:** bootstrap; RESOLVED.

## Demo LE fixups
- **Tried:** load demo CM_HI.EXE / CM_LO.EXE with dosre.
- **Failed because:** source type `0x06` (16:32 far pointer) is unsupported.
- **Better approach:** fix the LE loader in dosre with a regression test; do not
  patch the executable or pretend retail addresses describe the demo.
- **Session:** bootstrap; pre-established observation.

## Pure interpreter sound-card initialization
- **Tried:** run all original x86 with unmodified installed sound configuration.
- **Failed because:** Sounds::BootSOS returns failure during sound hardware setup;
  original main prints Sound System Config Error and exits before video mode.
- **Better approach:** original `-x` no-sound switch, proven at main
  `0x1F743/0x1F764`; BootSOS still initializes its original 40 Hz HMI timer.
  Do not enable the reference's Sounds game-function replacement.
- **Session:** bootstrap; RESOLVED by original `-x` title/new-game captures.
