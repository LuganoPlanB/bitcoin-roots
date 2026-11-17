# Preview the Bitcoin Roots website

Use Node.js 24 and npm. Open a terminal at the root of your `bitcoin-roots` checkout.

## First-time setup

The site uses the Plan ₿ theme from a sibling checkout. Clone the theme next to
`bitcoin-roots` and select the revision used by the [website build](../../.github/workflows/test-docs.yml).

```sh
git clone https://github.com/LuganoPlanB/vite-theme.git ../vite-theme
git -C ../vite-theme checkout --detach b969923b3407fb85a46cf45a8e18385d239cf79e
cd doc/website
npm ci
```

If you already have `../vite-theme`, skip the clone. If Vite reports that it
cannot resolve `lugano-planb-vite-theme/theme.css`, check that the two
repositories are side by side, then run `npm ci` in `bitcoin-roots/doc/website`.
This links the theme and installs the local VitePress used by the commands below.

## Preview while editing

Start the dev server in `doc/website`.

```sh
npm run dev
```

Open the local URL printed in the terminal. The page updates as you edit.
Press **Ctrl+C** to stop the server.

## Preview the release patches too

The patch browser is generated during the production build. To download the
public release inputs, build the full site, and serve it locally, run these
commands from `doc/website`.

```sh
npm run releases:build
npm run preview
```

See [Website release inputs](RELEASE-INPUTS.md) for the GitHub Pages base path
and release verification details.
