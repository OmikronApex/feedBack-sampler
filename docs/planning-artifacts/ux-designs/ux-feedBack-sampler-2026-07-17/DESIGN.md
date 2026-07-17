---
title: feedBack Sampler — DESIGN.md
status: final
created: 2026-07-17
updated: 2026-07-17
sources:
  - ../../prds/prd-feedBack-sampler-2026-07-17/prd.md
colors:
  bg: "#0f172a"
  surface: "#1e293b"
  surfaceInset: "#0b1220"
  sidebar: "#111827"
  primary: "#0ea5e9"
  primaryHover: "#38bdf8"
  onAccent: "#f8fafc"
  destructive: "#ef4444"
  text: "#f8fafc"
  textDim: "#94a3b8"
  border: "#334155"
  good: "#22c55e"
  mid: "#eab308"
  low: "#ef4444"
  gold: "#e8c040"
  focusRing: "#38bdf8"
typography:
  family: "Rubik, system-ui, sans-serif"
  weights: [300, 400, 500, 600, 700]
  scale:
    body: "0.875rem"
    label: "0.75rem"
    title: "1rem/600"
    header: "1.25rem/700"
rounded:
  control: "0.5rem"
  card: "0.75rem"
  pill: "999px"
  frame: "0.35rem"
spacing:
  unit: "0.25rem"
  cardPadding: "0.85rem 1rem"
  gutter: "1rem"
components:
  button: "filled {colors.primary}, text {colors.onAccent}, radius {rounded.control}, hover {colors.primaryHover}"
  knob: "ring-style rotary, track {colors.border}, value arc {colors.primary}, label {typography.scale.label} in {colors.textDim}"
  listRow: "surface transparent, hover {colors.surface}, selected left-accent 2px {colors.primary}"
  badge: "pill {rounded.pill}, format-colored fill at 20% opacity, label uppercase {typography.scale.label}"
  scrollbar: "6px, thumb {colors.border}, hover #475569, track transparent"
---

# feedBack Sampler — Design

> Visual reference: [mockups/key-screens.html](mockups/key-screens.html) — instrument view (generated controls + browser), Decent Sampler artwork-in-frame, settings overlay. On conflict, this spine wins over any mock.

## Brand & Style

feedBack Sampler is a fee[dB]ack-family product: dark, navy, focused, quietly confident. The instrument is the star — the chrome recedes. The `[dB]` wordmark motif appears once, in the header; no other branding competes with library artwork. Voice: precise and musician-friendly, never gamified (the plugin drops feedBack's playful arcade tones — this is a studio tool).

All tokens mirror the feedBack v3 `fb` palette (source of truth: `feedback/tailwind.config.js` + `static/v3/theme-core.js`). The plugin consumes the same role names so a future pass can inherit user-equipped feedBack themes via the `--fbv-*` variable contract. Legacy v2 tokens (`dark`, `accent`, legacy `gold` usage) are out of scope.

## Colors

- **Background** {colors.bg} for the plugin canvas; **surface** {colors.surface} for cards/panels; **inset wells** {colors.surfaceInset} for the library-artwork frame and value readouts.
- **Primary** {colors.primary} drives actions, selection, active states, progress/loading fills; hover {colors.primaryHover}.
- **Destructive** {colors.destructive} only for destructive/error (remove folder, failed load) — never decorative.
- **Status trio** {colors.good}/{colors.mid}/{colors.low} reserved for load/health states (loaded, loading/partial, failed).
- Format badges derive from the palette: SFZ {colors.primary}, SF2/SF3 {colors.gold}, Decent Sampler {colors.good} — 20% fills, full-strength text. [ASSUMPTION] Badge-color mapping is new (no upstream precedent); confirm or reassign freely.
- Text {colors.text} on any surface; secondary {colors.textDim}; hairlines {colors.border}.

## Typography

Rubik everywhere ({typography.family}), matching the v3 shell. Body {typography.scale.body}; control labels {typography.scale.label} in {colors.textDim}, uppercase for section labels; panel titles {typography.scale.title}; the header wordmark {typography.scale.header}. Numeric readouts (voice count, CC values) use tabular figures.

## Layout & Spacing

4px base unit ({spacing.unit}). Card padding {spacing.cardPadding}; {spacing.gutter} gutters between panes. The plugin is freely resizable with a minimum of 720×480 logical px; layout reflows (library browser collapses to overlay below 900px width). [ASSUMPTION] Min size and breakpoint — validate against real DS artwork sizes during build.

## Elevation & Depth

Flat-first like v3: depth from surface-tone steps ({colors.bg} → {colors.surface} → {colors.surfaceInset}), not shadows. Modals/overlays get a scrim (black 60%) and a hairline {colors.border}; no drop-shadow language elsewhere.

## Shapes

Controls {rounded.control}; cards and panes {rounded.card}; badges and toggles {rounded.pill}; the library-artwork frame {rounded.frame} inset in a {colors.surfaceInset} well. Focus-visible: 2px {colors.focusRing} outline, 2px offset — identical to v3.

## Components

- **Header bar** — wordmark, loaded-library name (primary text), format badge, voice-count readout, settings gear. Height 48px.
- **Button** — per {components.button}; secondary variant: transparent fill, {colors.border} border, {colors.text} label.
- **Knob / slider** — per {components.knob}; double-click resets; value tooltip on drag ({colors.surfaceInset} pill).
- **Library list row** — per {components.listRow}: name, format badge, size {colors.textDim}; failed rows show {colors.low} status dot with reason on hover.
- **Format badge** — per {components.badge}.
- **Artwork frame** — Decent Sampler (and future skinned) UIs render unmodified inside the frame; the frame, not the artwork, carries feedBack identity.
- **Progress** — thin 2px {colors.primary} fill on {colors.border} track (library scan, async load).

## Do's and Don'ts

- **Do** let library artwork be itself inside the frame; **don't** recolor or restyle author-supplied graphics.
- **Do** keep chrome monochrome-navy with one primary accent; **don't** introduce colors outside the token set.
- **Don't** import feedBack's gamification visuals (badges, arcade gold accents) — {colors.gold} appears only in the SF2/SF3 format badge.
- **Do** keep error text selectable/copyable (v3 policy); **don't** block text selection in error states, paths, or the settings pane.
