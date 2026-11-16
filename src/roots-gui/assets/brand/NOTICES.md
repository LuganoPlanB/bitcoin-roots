# Identity sources and rights

The reviewed Plan B Foundation Civic Digital Commons `tokens.json` and
`brand.json` were retrieved 2026-11-15 from
`https://plan-b.foundation/branding/`. Their exact SHA256 values are
`2ade02257affd47398579383bdcb816f370c2848b18b4d135777d7b96dd328ef` and
`2053ae7a0e8105ee796c2546eebbef5743e77e221020877fd00b21125adc044a`.
Original assets retain existing rights; no new blanket trademark or third-party
license is granted. The JSON is preserved unchanged as the published layer.

The three Bitcoin Roots SVG identities match every SHA256 in the preserved
`roots-manifest.json`, from
`https://plan-b.foundation/branding/assets/projects/bitcoin-roots/manifest.json`.
All use intrinsic `viewBox="0 0 512 512"`; no geometry was changed. The color
source is the MIT-licensed Bitcoin Roots project artwork, commit
`ebdc72c0cd25603ddc89026faef9fc7bf5d6a538`, path
`src/qt/res/src/bitcoinroots-logo.svg`, also retained in this repository.
The black/white variants replace only `#F57C00` with `#000000`/`#FFFFFF` as
specified by the reviewed manifest. Original project artwork retains its
existing rights. No new trademark rights or endorsement are granted.

Use black on light and white on dark when monochrome is needed; the color
identity always retains Roots artwork orange `#F57C00`. Preserve square
proportions. The Network Energy identity band uses the user-specified rainbow
stops, never the separate published hero/heading gradients.

Inter Regular/SemiBold remain unmodified bundled ImKit font inputs with their
SIL OFL 1.1 notice, copied beside the executable. The technical face is the
scalable embedded ProggyForever minimal font from pinned ImGui's
`imgui_draw.cpp`: MIT, Copyright (c) 2026 Disco Hello; Copyright (c) 2019,2023
Tristan Grimmer. Its original notice and bytes remain in the source archive.
See `../../third_party/NOTICES.md` for complete source and license locations.
