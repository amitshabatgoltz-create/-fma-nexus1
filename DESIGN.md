# DESIGN.md · FMA Nexus-1

**Direction: backstage gear.** The glove is shown like gear under stage light: a dark graphite room, one cold white light for actions, and the product's own three button colors used only where they mean a program. Type mixes a tall condensed display (like a tour poster or a drop tag) with a calm technical body face and mono for specs. Built for musicians and gamers aged roughly 14 to 30, in Hebrew, right to left.

Not based on compared reference sites (UXKIN was not connected). It keeps the existing brand: dark theme, white LED accent, red / green / blue buttons.

## Color

One theme: dark. The site never switches to light.

| Token | Value | Role |
|---|---|---|
| `--bg` | `#0B0B0E` | Page background |
| `--bg-2` | `#111116` | Alternate section band |
| `--card` | `#17171D` | Cards, inputs on bands, panels |
| `--card-2` | `#1E1E26` | Raised or selected surfaces |
| `--line` | `rgba(255,255,255,.09)` | Hairlines, card borders |
| `--line-2` | `rgba(255,255,255,.18)` | Input borders, strong dividers |
| `--text` | `#F3F3F6` | Body text, headings (17:1 on bg) |
| `--muted` | `#A6A6B4` | Secondary text (8:1 on bg) |
| `--led` | `#EEF4FF` | **Primary**: buttons, selected state, focus glow |
| `--green` | `#3DDC84` | Green program, success messages |
| `--blue` | `#5B9BFF` | Blue program, focus outline, links in forms |
| `--red` | `#FF5C5C` | Red program, low stock, errors |

Rules: red, green and blue appear only for their program, for state (success, error, low stock) or as the three buttons in drawings. Never as decoration. No gradients on text. The only glow is the white LED glow on the hero product and the primary button hover.

## Typography

| Token | Family | Use |
|---|---|---|
| `--display` | Karantina 700, fallback Rubik, Arial | Hero headline, section titles, big numbers |
| `--body` | IBM Plex Sans Hebrew 400/500/600/700, fallback Heebo, Arial | Everything else |
| `--mono` | IBM Plex Mono 500, fallback ui-monospace | Specs, prices in tags, drop numbers (Latin and digits only) |

Scale (rem / line-height): display-xl clamp(4rem, 11vw, 8.5rem)/0.92 · display-l clamp(2.8rem, 6vw, 4.8rem)/0.95 · h3 1.4/1.25 · lead 1.2/1.6 · body 1.0625/1.7 · small 0.9/1.55 · micro 0.78/1.4 (mono, letter-spacing .06em).

Body text 17px, lines at most 60 characters. Karantina is tall and narrow, so it only goes at 2.8rem and up.

## Spacing

8px base: 4, 8, 12, 16, 24, 32, 48, 64, 96, 128. Section padding 96px (64px on phones). Content width 1180px, text width 60ch. Side gutter at least 20px.

## Shape and depth

Radius: 6px small (inputs, chips), 14px cards, 999px buttons. No drop shadows. Depth comes from surface steps (`--bg` → `--card` → `--card-2`) and hairlines.

## Components

- **Primary button**: `--led` fill, `--bg` text, pill, min-height 52px. Hover: LED glow. Focus: 2px `--blue` outline. Disabled: 40% opacity.
- **Secondary button**: transparent, `--line-2` border, `--text`. Hover: border `--text`.
- **Quiet link**: `--muted`, underline on hover.
- **Inputs**: `--bg` fill, `--line-2` border, 6px radius, 16px text. Focus: `--blue` border + 3px ring.
- **Choice chips** (side, size): 3-column grid, selected = `--led` border and inset ring.
- **Spec tag**: mono micro text in a 1px `--line-2` box, e.g. `DROP 02 · 10 PCS`.
- **Cards**: `--card`, 1px `--line`, 14px radius. Only where the content is a separate object (review, order form, program panel).
- **Navigation**: sticky, `--bg` at 80% with blur, logo right, links, primary button left.
- **Footer**: 4 columns that stack on phones, muted links.

## Imagery and icons

- The real glove photo is the hero. Videos are real demo clips. AI renders are allowed only when marked as a render.
- Icons: simple 1.5px stroke inline SVG, 20 to 24px, in `--text` or `--muted`. No emoji in the UI.
- The three program colors appear as round "buttons", the same as on the real control unit.
