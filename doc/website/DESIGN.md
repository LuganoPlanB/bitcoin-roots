---
name: Bitcoin Roots Documentation
description: A source-first operator experience in the Lugano Plan ₿ civic technology system.
colors:
  civic-night: "#082952"
  civic-sky: "#4f97e9"
  commons-gold: "#ffb604"
  civic-ink: "#030b20"
  warm-canvas: "#fffefa"
  cool-canvas: "#f3f9ff"
  quiet-panel: "#fcfcfc"
  dark-canvas: "#171717"
typography:
  display:
    fontFamily: "Inter, Segoe UI, Arial, sans-serif"
    fontSize: "clamp(3.45rem, 6.6vw, 6rem)"
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

**Creative North Star: "The Independent Signal"**

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
- One authored signal motion; interaction state does the rest.

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
- **Dark Canvas:** the explicit dark-mode ground.

**The One Signal Rule.** Gold marks one live choice or route position; it does not
decorate collections.

## Typography

**Display Font:** Inter, with Segoe UI and Arial fallbacks

**Body Font:** Inter, with Segoe UI and Arial fallbacks
**Label/Mono Font:** Consolas or SFMono-Regular for paths, code, and measurements

**Character:** One robust sans-serif voice shifts from monumental operator
propositions to compact technical reference through scale and weight alone.

### Hierarchy

- **Display:** extra-bold, fluid, tight, and capped at 6rem for the homepage thesis.
- **Headline:** extra-bold and fluid for major section propositions.
- **Title:** compact, strong labels for steps, stages, and document groups.
- **Body:** regular-weight copy at a relaxed 1.68 line-height and readable measure.
- **Label:** compact uppercase metadata with restrained tracking.

**The Plain Source Rule.** Monospace identifies literal paths, code, or measured
state; it never acts as a generic technical costume.

## Layout

Persuasive surfaces use a centered 1240px field with fluid gutters and asymmetric
columns. The homepage begins with message on the left and a larger signal
instrument on the right. Reading pages keep the VitePress documentation frame;
the atlas uses paired ruled lists rather than card grids. At 960px major pairs
stack, and at 720px signal, boundary, library, and atlas structures become a
single readable sequence. Section spacing expands to roughly 5–8rem on wide
screens and contracts without crowding on mobile.

**The Source Map Rule.** Large collections expose both human titles and repository
paths; navigation may organize the corpus but never obscure its provenance.

## Elevation & Depth

Depth is ambient and sparse. The signal instrument uses the inherited Plan ₿
panel shadow; smaller boundary panels use a soft downward lift. Ruled lists,
sidebars, and reading content stay flat and use tonal changes or fine blue rules.

**The Instrument Rule.** Shadow identifies a focused interactive instrument, not
every container on the page.

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

### Signal Instrument

Four keyboard-focusable stages share a vertical route. Focus or activation moves
the highlighted row, gold node, measured position, and live explanatory readout.
The pulse travels once through this system and is suppressed for reduced motion.

### Documentation Atlas

Groups use strong ruled summaries, counts, titles, and literal source paths.
Release history starts collapsed; filtering opens matching groups and provides a
polite live result count plus a recoverable empty state.

## Do's and Don'ts

### Do:

- **Do** lead operators toward a concrete next step within the first viewport.
- **Do** distinguish consensus from local policy through both copy and structure.
- **Do** keep source paths visible wherever documentation is aggregated.
- **Do** preserve keyboard focus, reduced motion, and the responsive reading order.

### Don't:

- **Don't** replace source material with a rewritten documentation copy.
- **Don't** scatter gold, shadows, or motion across document collections.
- **Don't** use speculative coin art, terminal cosplay, or security-theatre chrome.
- **Don't** turn the atlas into a repeated icon-card grid.
