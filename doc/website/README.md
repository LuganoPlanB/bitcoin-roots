# Preview the Bitcoin Roots website

Use Node.js 24 and npm. From the root of your Bitcoin Roots checkout, install
the website dependencies and start the development server.

```sh
cd doc/website
npm ci
npm run dev
```

Open the local URL printed in the terminal. The page updates as you edit.
Press **Ctrl+C** to stop the server. If Vite reports a missing
`lugano-planb-vite-theme/theme.css` after updating your checkout, rerun
`npm ci` here. It installs both VitePress and the pinned Plan ₿ theme.

## Build and preview the full site

The production build includes the patch browser. Download its release data
once, then build and serve the site from `doc/website`.

```sh
npm run releases:prepare
npm run build
npm run preview
```

Later builds can reuse the downloaded data. Run `npm run releases:prepare`
again to pick up new releases. See [Website release inputs](RELEASE-INPUTS.md)
for the GitHub Pages base path and release verification details.

## Refresh the RPC reference

The command pages work offline in a local preview. To refresh them from the
latest stable release on x86-64 Linux, use Python 3.11 or later.

```sh
python3 scripts/rpc-reference.py
```

The script verifies the Linux package against GitHub’s release digest and runs
`getopenrpcinfo` and `help <command>` on an isolated regtest node. Pages builds
refresh the same URLs when the stable release changes. When reviewing a new
release, check the short Core comparison notes in `scripts/rpc-reference.py`
for changes to existing commands as well as new ones.
