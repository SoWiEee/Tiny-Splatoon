# Player UI direction

Confirmed by the user on 2026-10-23: replace the previous minimal menu direction with a bold, source-inspired game interface. The native UI should feel like a lively lobby: warm paper panels, dark ink outlines, punchy orange actions, cobalt-blue secondary actions, and clear team-color accents. Preserve the existing gameplay and menu behavior in `spec.md` and `PRODUCT.md`.

Use flat, high-contrast surfaces with confident type and compact, deliberate rules. Keep the character preview lane visible at the right of the menu. Give navigation, tabs, selected modes, buttons, inputs, and checkboxes distinct hover, pressed, selected, disabled, and keyboard-focus states. Keep focus visible without relying on color alone; use a strong border and fill change. Use short color transitions only, and honor the reduced-motion setting where the application already applies it.

Group settings by task and separate groups with ink rules. The menu content column is explicitly bounded between the 94dp header and the 46dp footer; it scrolls without moving navigation or the return action. The final accessibility settings remain reachable at 720p and 1080p. Keep online roster scrolling bounded and usable. Preserve all current menu IDs, actions, and the preview placement contract.

- Palette: warm paper `#f4ecdc`, near-black ink `#211d27`, action orange `#f28a24`, cobalt blue `#2f5bff`, pale cream `#fff8ea`.
- Typography: Inter for Latin headings, labels, and numbers; retain local Noto Sans TC fallback for Traditional Chinese glyphs.
- Avoid generic dashboard cards, neon glow, gradients, large blurred shadows, decorative noise, and repeated entrance animation.
- Use only the existing RmlUi runtime, native RCSS capabilities, and locally shipped fonts; do not add dependencies.

Reuse the reference squid emblem and weapon SVG artwork through the checked 128px PNG export in `assets/ui/weapons`; the shipped game needs no browser. Main settings labels and controls use 18dp or larger text.

The HUD remains a separate gameplay layer. Keep the clock, team score, squad roster, health/ink, special readiness, and mode-specific information readable over the scene, using orange and blue for team identity.
