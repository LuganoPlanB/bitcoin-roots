---
name: Bitcoin Roots Documentation
description: A source-first operator experience in the Lugano Plan ₿ civic technology system.
colors:
  civic-night: "#082952"
  civic-sky: "#4f97e9"
  civic-link: "#246caf"
  civic-link-hover: "#17578f"
  commons-gold: "#ffb604"
  civic-ink: "#030b20"
  civic-ink-soft: "rgba(3, 11, 32, 0.72)"
  warm-canvas: "#fffefa"
  cool-canvas: "#f3f9ff"
  quiet-panel: "#fcfcfc"
  quiet-panel-strong: "#f3f8fd"
  dark-canvas: "#171717"
  dark-canvas-alt: "#1a1717"
  dark-panel: "#211d1d"
  dark-panel-strong: "#292424"
  dark-accent: "#e15364"
  dark-highlight: "#f7931a"
  focus-violet: "#7468ff"
typography:
  display:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "clamp(3.45rem, 6.15vw, 5.75rem)"
    fontWeight: 800
    lineHeight: 1.05
    letterSpacing: "-0.035em"
  headline:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "clamp(2.4rem, 5vw, 4.5rem)"
    fontWeight: 800
    lineHeight: 1.05
    letterSpacing: "-0.035em"
  title:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "1.15rem"
    fontWeight: 800
    lineHeight: 1.25
  body:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.68
  label:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "0.76rem"
    fontWeight: 800
    lineHeight: 1.3
    letterSpacing: "0.06em"
  attribution:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "0.82rem"
    fontWeight: 800
    lineHeight: 1.3
    letterSpacing: "0.06em"
  identity:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "clamp(1.8rem, 3vw, 2.75rem)"
    fontWeight: 800
    lineHeight: 1.04
    letterSpacing: "-0.035em"
  reading-display:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "clamp(2.8rem, 6vw, 5rem)"
    fontWeight: 800
    lineHeight: 1.02
    letterSpacing: "-0.025em"
rounded:
  control: "12px"
  panel: "16px"
  pill: "999px"
spacing:
  compact: "0.75rem"
  content: "1.5rem"
  section: "5rem"
components:
  button-primary:
    backgroundColor: "{colors.civic-night}"
    textColor: "{colors.warm-canvas}"
    typography: "{typography.label}"
    rounded: "{rounded.pill}"
    padding: "0.72rem 1.05rem"
    height: "2.875rem"
  panel:
    backgroundColor: "{colors.quiet-panel}"
    textColor: "{colors.civic-ink}"
    rounded: "{rounded.panel}"
    padding: "clamp(2rem, 4vw, 3.25rem)"
  search-field:
    backgroundColor: "{colors.quiet-panel}"
    textColor: "{colors.civic-ink}"
    rounded: "{rounded.control}"
    padding: "0.8rem 1rem"
    height: "3.25rem"
---

# Design System: Bitcoin Roots Documentation

## Overview

**Creative North Star: "The Plan ₿ Editorial Signal"**

The website extends the Lugano Plan ₿ Civic Digital Commons into an operator-first
documentation experience. Independence is made visible through precise signal
paths, explicit boundaries, and source locations rather than security theatre or
speculative Bitcoin imagery.

The atmosphere is open, civic, and technically serious. Large navy propositions
create confidence; cool fields organize dense material; a single gold signal marks
the active point of choice.

**Key Characteristics:**

- White-first civic clarity with a complete dark alternative.
- Asymmetric persuasive surfaces paired with disciplined reading layouts.
- Source paths and technical data remain visible and legible.
- A static first viewport that pairs the operator proposition with Plan ₿
  attribution and the Bitcoin Roots identity.

## Colors

Navy and sky blue carry structure, warm white and cool blue separate reading
zones, and gold is reserved for the active signal.

### Primary

- **Civic Night:** headings, structural emphasis, and primary light-theme actions.
- **Civic Sky:** links, rules, active metadata, and documentation wayfinding.

### Secondary

- **Commons Gold:** the one exceptional signal—active nodes and route position.

### Neutral

- **Civic Ink:** primary copy.
- **Warm Canvas:** the main reading ground.
- **Cool Canvas:** operator paths, sidebars, and broad section changes.
- **Quiet Panel:** focused instruments, cards, and inputs.
- **Quiet Panel Strong:** table labels and emphasized quiet surfaces.
- **Dark Canvas / Canvas Alt:** the explicit dark-mode reading grounds.
- **Dark Panel / Panel Strong:** dark-mode layered surfaces.
- **Dark Accent / Highlight:** rose wayfinding and orange active emphasis in dark mode.
- **Focus Violet:** the shared visible keyboard-focus outline in both themes.

**The One Signal Rule.** Gold marks one live choice or route position; it does not
decorate collections.

## Typography

**Display Font:** Inter, with Segoe UI and Arial fallbacks

**Body Font:** Inter, with Segoe UI and Arial fallbacks
**Label/Mono Font:** Consolas or SFMono-Regular for paths, code, and measurements

**Character:** One robust sans-serif voice shifts from monumental operator
propositions to compact technical reference through scale and weight alone.

### Hierarchy

- **Display:** extra-bold, fluid, tight, and capped at 5.75rem for the homepage thesis.
- **Headline:** extra-bold and fluid for major section propositions.
- **Title:** compact, strong labels for steps, stages, and document groups.
- **Body:** regular-weight copy at a relaxed 1.68 line-height and readable measure.
- **Label:** compact uppercase metadata with restrained tracking.
- **Attribution:** compact uppercase Plan ₿ provenance above the hero identity.
- **Identity:** tightly set logo tagline within the static hero editorial block.
- **Reading Display:** a narrower fluid title for long-form Principles and Compare pages.

**The Plain Source Rule.** Monospace identifies literal paths, code, or measured
state; it never acts as a generic technical costume.

## Layout

Persuasive surfaces use a centered 1240px field with fluid gutters and asymmetric
columns. The homepage begins with the operator proposition on the left and a
static editorial identity on the right: the requested Plan ₿ attribution sits
above the Bitcoin Roots logo and tagline. At 960px major pairs stack. At 720px
the hero reading order becomes attribution, proposition, then logo/tagline;
signal, boundary, library, and atlas structures also become a single readable
sequence. Actions become full-width below 420px.

Long-form Principles and Compare content keeps the VitePress reading frame,
uses a 56rem container and a 72ch prose measure. Compare alone expands its
desktop container to the full 1240px field while keeping surrounding prose to
52rem. Its four-column table fills that field; below 720px it retains a 58rem
minimum width inside an explicitly labelled, keyboard-focusable horizontal
scroll region with the row-heading column held sticky. Section spacing expands
to roughly 5–8rem on wide screens and contracts without crowding on mobile.

**The Source Map Rule.** Large collections expose both human titles and repository
paths; navigation may organize the corpus but never obscure its provenance.

## Elevation & Depth

Depth is ambient and sparse. The editorial hero stays flat and uses fine blue
rules; smaller boundary panels use a soft downward lift. Ruled lists, sidebars,
and reading content stay flat and use tonal changes or fine blue rules.

**The Instrument Rule.** Shadow identifies a focused interactive control, not
editorial structure or every container on the page.

## Shapes

Major instruments use precise 16px corners; controls and focused rows use 12px;
only compact actions use full pills. Signal nodes are small circles tied to
one-pixel routes. Borders remain thin and structural.

## Components

### Buttons

- **Shape:** compact pill with a minimum 46px target.
- **Primary:** Civic Night on Warm Canvas; gold on dark surfaces where needed.
- **Hover / Focus:** a restrained color shift and the shared violet focus outline.
- **Quiet:** underlined text with generous underline offset.

### Cards / Containers

- **Corner Style:** 16px for instruments and primary panels; 12px for focused rows.
- **Background:** Quiet Panel or the inherited layered panel background.
- **Shadow Strategy:** ambient only for focused instruments and paired boundaries.
- **Internal Padding:** fluid 2–3.25rem for major instruments.

### Inputs / Fields

- **Style:** Quiet Panel, thin sky-blue stroke, 12px corners, visible native caret.
- **Focus:** shared violet outline with offset.
- **Empty:** a direct explanation and a pill action to clear the filter.

### Navigation

The VitePress shell stays compact and bold. Active and hover states use Civic Sky;
mobile keeps search and the menu in the familiar theme layout.

### Editorial Signal

The hero pairs the operator proposition with a static Plan ₿ editorial identity.
The attribution eyebrow reads as provenance, while the Bitcoin Roots logo and
tagline form one identity block. The following four-step verification sequence
uses literal ordering, fine rules, and restrained accent numerals; neither the
hero nor the sequence cycles or advances over time.

### Root Mark

Use the supplied Bitcoin Roots SVG as a static identity asset in the hero,
lineage, and closing surfaces. The homepage contains no Lottie section and does
not load or run the legacy Lottie asset.

### Comparison Table

Use the comparison treatment for sourced, side-by-side distinctions: a dark
header, emphasized row headings, subtle alternating rows, and links that inherit
the header contrast. Keep the complete four-column table on narrow screens and
make horizontal scrolling discoverable with visible instructional text rather
than collapsing or hiding comparisons.

### Documentation Atlas

Groups use strong ruled summaries, counts, titles, and literal source paths.
Release history starts collapsed; filtering opens matching groups and provides a
polite live result count plus a recoverable empty state.

## Do's and Don'ts

### Do:

- **Do** lead operators toward a concrete next step within the first viewport.
- **Do** distinguish consensus from local policy through both copy and structure.
- **Do** keep source paths visible wherever documentation is aggregated.
- **Do** preserve the shared 2px violet focus outline with 3px offset; give the
  comparison scroll region enough extra offset to remain visible.
- **Do** preserve the responsive reading order and reduce smooth scrolling,
  transitions, and animation to effectively immediate behavior when reduced
  motion is requested.

### Don't:

- **Don't** replace source material with a rewritten documentation copy.
- **Don't** scatter gold, shadows, or motion across document collections.
- **Don't** use speculative coin art, terminal cosplay, or security-theatre chrome.
- **Don't** turn the atlas into a repeated icon-card grid.
- **Don't** reintroduce Lottie or timed animation into the static homepage identity.
