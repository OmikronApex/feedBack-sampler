# Reconciliation — PRD vs. DESIGN.md + EXPERIENCE.md

Input: `docs/planning-artifacts/prds/prd-feedBack-sampler-2026-07-17/prd.md`

| PRD requirement | Landed in |
|---|---|
| FR-21 three surfaces (library list / instrument view / settings) | EXPERIENCE.md IA — all three, all mocked |
| FR-20 corporate design, artwork inside frame | DESIGN.md tokens (v3 source-extracted) + artwork-frame component |
| FR-12 library-defined + generic controls | EXPERIENCE.md Component Patterns; DESIGN.md knob spec |
| FR-8 search/filter, switch without dropouts | EXPERIENCE.md library list (crossfade-on-ready) |
| FR-9 soundfont preset browsing | EXPERIENCE.md preset selector row |
| FR-5 human-readable load failure | State Patterns (status dot + reason; previous library kept) |
| FR-14 async load, playable-when-ready | State Patterns + Flow 1 climax |
| FR-16 state restore | State Patterns (silent restore + Locate recovery card) |
| FR-17 automatable parameters | Component Patterns (host-automatable + MIDI-learn) |
| UJ-1 Lena / UJ-2 Marco | Flows 1–2 mirror protagonists verbatim; Flow 3 adds recovery |
| Glossary terms (library, soundfont preset, plugin state) | Used consistently across both spines |

Gaps: none blocking. Note: PRD "no MIDI hint" and drag-and-drop are UX additions beyond PRD scope — tagged [ASSUMPTION], approved with mocks 2026-07-17. Verdict: no spine changes required.
