# Linux large-wallet qualification

The native qualification uses the production X11/GLFW/OpenGL window and
workspace with public fixture data. Its reference targets are 16.7 ms at p95
for frame preparation and renderer submission, and under 100 ms for local
command feedback. Use the report's recorded hardware, software renderer,
window scale, build, and measurement method when comparing results.

The fixture supplies 100,000 history observations and 100,000 outpoints to a
controlled source. The workspace exercises the existing refresh budgets,
including explicit continuation through the 4,096 retained history bound and
the 8,192 dated UTXO bound. Incomplete observations retain their original
qualification and do not authorize funding or show a reconciled balance. A
separate authenticated loopback check exercises rejection of 100,000 decoded
history entries and the UTXO query's one-extra-row sentinel through the
production fetch function.

The Send fixture reads a 1,000-recipient CSV through the local worker. It
exercises a paged edit, the validation error, and application of the repaired
draft. The current funding limit of 64 recipients still applies. The
workbench fixture opens a 60,110-byte PSBT with 512 inputs and 768 outputs
through the local file worker. It exercises the overview, a bounded hex
page, pagination and repeated open/close. No signatures are produced and
nothing is submitted.

Build the native measurement target and run the fixture from a configured
Linux standalone build.

```sh
cmake --build build -j2 --target roots_gui_large_wallet
LP_NUM_THREADS=4 LC_ALL=C.UTF-8 python3 -B tests/visual/run_large_wallet.py \
  --binary build/roots_gui_large_wallet --fonts build/roots-gui-assets/fonts \
  --output /absolute/path/to/performance-evidence
```

Use the same command with `--admission` for the separate loopback check. The
fixture runner owns its X11 display, temporary metadata and loopback service.
Use a private, writable temporary directory and a persistent evidence
directory. The exact fixture bytes, raw timing samples, native framebuffers,
published semantics, resource traces, and process exits are retained.

`ui_ns` measures owner-thread refresh polling and workspace drawing.
`idle_to_render_ns` starts after host event polling and ends after OpenGL
submission, before presentation. Consecutive idle timestamps also expose
the actual frame cadence, which includes swap, input/event polling, and
measurement callbacks. Framebuffer readback frames are excluded from timed
samples, and adjacent cadence entries that include a readback are omitted.
The report separates the first 24 frames per view from later frames and uses
nearest-rank percentiles. Software rendering and XTest feedback in a virtual
display do not measure physical display presentation or hardware input
latency.

A changed observation, sort, filter, or edit can incur a cold projection or
validation cost. Warm rows are clipped or paged. Work counts identify the
projection invalidations and the work submitted on each frame, alongside the
elapsed timings. Process RSS includes fixture allocations and allocator or
driver high-water retention. Compare live tasks and descriptors after
settlement alongside RSS when investigating lifetime regressions.
