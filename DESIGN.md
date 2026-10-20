# Player UI direction

Confirmed by the user on 2026-10-20: clean and minimal, with fewer decorations than `splat-ref/`. This supersedes the reference's decorative typography and ink-heavy menus; functional requirements remain in `spec.md`.

The player chooses a match on a desktop monitor and needs to recognize actions and settings quickly. Use a light, quiet menu surface, a restrained dark-green action color and high-contrast compact overlays during play. Retain orange and blue for team information. No decorative splats, tilted cards, heavy outlines or repeated entrance animations.

- Inter for Latin headings, labels and tabular numbers; Noto Sans TC for Traditional Chinese and missing glyphs. Ship fonts locally with their licenses.
- Regular weights for body text, medium/semibold for actions and headings. Favor clear scale and spacing over decoration.
- Group setup controls by task; use rows and separators instead of nested cards. Keep the primary start action clear and the exit action secondary.
- Show explicit hover, pressed, selected, disabled and keyboard-focus states. Use modest color/opacity transitions; honor reduced-motion preferences when exposed.
- HUD keeps time, team score, health/ink and special readiness readable over the scene, without development instructions in the primary information area.
- Preserve actual mode/loadout selection and pause/return/close actions. Do not add nonfunctional menu destinations.

Native implementation: RmlUi renders into the existing OpenGL context after scene postprocessing. RML/RCSS live under `assets/ui`; gameplay state remains owned by `src/game`.
