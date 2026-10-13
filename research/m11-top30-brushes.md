# Krita Artists: top 30 brush resources (milestone 11 research)

## How the list was taken

- **Source:** `https://krita-artists.org/c/resources/brushes-and-bundles/32/l/top`, read as the
  Discourse JSON of the same view (`…/l/top.json`). The site's default period for this view is
  "all" (`for_period: "all"` in the response). The explicit `?period=all` view returned the same
  order.
- **Accessed:** 2026-10-12, 23:59 UTC. Rankings change over time; this is the order at that moment.
- **Per resource:** the first post of each topic (`/t/<id>.json`) was read for the author's
  description, links and stated licence.
- **Engines, verified:** where a bundle could be downloaded without a browser session
  (`files.kde.org`, GitHub releases, Dropbox, peppercarrot.com, public Google Drive files), it was
  downloaded **into a temporary folder for inspection only**. Each `.kpp` preset's settings were
  read from its PNG `preset` text chunk:
  - the engine (`paintopid`);
  - the tip type;
  - texture enabled and mode;
  - masked (dual) brush;
  - composite op;
  - smudge mode;
  - lightness tips;
  - the sensors in use.

  Nothing downloaded was added to DrawApp or redistributed. Scripts: `ka_posts.py`,
  `ka_fetch.py`, `ka_features.py` (session scratch, not part of the repository).
- **Engines, unverified:** for 19 of the 30 resources the files were behind a store, Mega,
  MediaFire, Proton Drive or a Drive confirmation page, or the topic links to an external
  collection. Their entries say **unverified** and rely on the author's text only.
- **DrawApp status** means whether DrawApp can produce the *kind* of effect with its own engine.
  DrawApp **cannot import any of these presets** (no `.kpp` or bundle importer exists,
  `docs/brush-preset-format.md`), and no preset is claimed to be supported because a similar
  engine exists (specification §3.D).

## Ranking (as accessed)

| # | Topic | Likes | Views | Posted | Engine(s) | Verified? | Licence stated |
|---|---|---|---|---|---|---|---|
| 1 | Ink Pen (CSP-like) | 623 | 211 489 | 2022-09-06 | pixel ×1 | **yes** | not stated |
| 2 | Sketch V2 (SK2), coloured pencils and blenders | 1158 | 178 604 | 2023-03-08 | pixel ×24, colour smudge ×8 | **yes** | not stated |
| 3 | Set of Watercolor Brushes (Pesi) | 1014 | 182 788 | 2020-02-28 | unknown | no (Gumroad) | not stated (free) |
| 4 | Metallics by Draneria | 632 | 72 639 | 2024-11-08 | unknown | no | CC-BY-SA |
| 5 | Yet another brush & texture promo – tombofnull | 436 | 90 938 | 2024-10-27 | external collection | no (itch.io) | not stated |
| 6 | Memileo Impasto Brushes | 911 | 157 255 | 2024-06-03 | colour smudge (colour rate, dulling named in post) | partly (text) | CC-BY-SA |
| 7 | INK brushes (Ink Pro Artist, InkT) | 1394 | 152 158 | 2023-06-21 | pixel ×37, MyPaint ×2, colour smudge ×1, spray ×1 | **yes** | not stated |
| 8 | Mikhailasmo's Inking Brush Set | 370 | 97 980 | 2024-04-20 | unknown | no (Proton Drive) | not stated |
| 9 | Deevad 2025-01 Brush Bundle | 348 | 101 460 | 2023-01-18 | pixel ×39, colour smudge ×5, experiment ×1, deform ×1 | **yes** | CC-0 |
| 10 | Rakurri Brush Set V2 | 671 | 218 736 | 2021-12-23 | pixel ×74, colour smudge ×37, deform ×10, filter ×8, sketch ×4, spray ×4, clone ×3, experiment ×2, round marker ×1, MyPaint ×1 | **yes** | not stated in post |
| 11 | Mojo Moo for comics | 360 | 85 662 | 2023-08-30 | unknown | no (repository holds only a README) | not stated |
| 12 | Lur Noise's brush pack | 333 | 80 804 | 2023-12-11 | unknown | no (itch.io) | not stated |
| 13 | wojtryb "Won't teach you to draw" v6.0 | 289 | 104 890 | 2020-10-19 | pixel ×60, colour smudge ×15, experiment ×3 | **yes** | not stated |
| 14 | Rotating light brushtips WIP | 1484 | 125 463 | 2023-05-04 | unknown (animated RGBA tips, rotation by drawing angle) | no | not stated |
| 15 | Chaos ink brushes | 259 | 66 352 | 2024-11-26 | unknown | no (paid) | paid |
| 16 | Concept & Illustration V2 | 275 | 52 936 | 2025-01-17 | pixel ×120, colour smudge ×7 | **yes** | not stated |
| 17 | Peter Mohrbacher – Angelarium brushes and gradients | 266 | 47 695 | 2025-09-29 | external store | no | not stated |
| 18 | Brush pack for comics and illustrations (LM) | 279 | 49 376 | 2023-11-20 | unknown | no (Patreon/Dropbox) | free to use (post) |
| 19 | Awez Flat Brushes | 248 | 56 619 | 2023-09-06 | unknown ("graphics tablet exclusive functionality") | no | not stated |
| 20 | Gege Akutami line brushes | 261 | 90 536 | 2024-01-17 | unknown | no (paid) | paid |
| 21 | Cheap Ballpoint pen Brush | 401 | 64 525 | 2021-01-18 | pixel ×2 | **yes** | not stated |
| 22 | Wavenwater concept art traditional medium | 303 | 69 145 | 2023-03-22 | unknown (markers) | no | not stated |
| 23 | Val's Realistic Ink Pen | 278 | 40 790 | 2025-10-16 | pixel ×1 | **yes** | not stated |
| 24 | Hair Brushes (A gift for Christmas) | 248 | 40 145 | 2023-12-26 | unknown | no (Drive confirmation page) | not stated |
| 25 | Dry & Dirty Bundle | 485 | 48 231 | 2024-01-22 | unknown | no (Mega) | not stated |
| 26 | JP Color Mixer Brushes | 255 | 37 547 | 2025-03-30 | unknown (post describes colour-rate mixing) | no (MediaFire) | uses others' tips, per post |
| 27 | My Bristle Engine Brushes | 212 | 48 270 | 2024-12-27 | bristle (stated by author) | partly (text) | not stated |
| 28 | FizzyFlower's ESSENTIAL Brushset V2 (400+) | 287 | 124 753 | 2022-03-20 | unknown | no | not stated |
| 29 | Hard Brushes Pack | 228 | 55 160 | 2024-05-31 | unknown | no (Gumroad) | not stated |
| 30 | Glitches and distortion | 189 | 34 571 | 2024-01-06 | colour smudge ×7 | **yes** | "Feel free to use" |

## What the verified presets actually use

11 bundle files from 10 topics (#1, 2, 7, 9, 10, 13, 16, 21, 23, 30), **479 presets**:

| Engine | Presets | Share |
|---|---|---|
| Pixel (`paintbrush`) | 358 | 75 % |
| Colour smudge (`colorsmudge`) | 80 | 17 % |
| Deform | 11 | 2 % |
| Filter | 8 | 2 % |
| Experiment | 6 | 1 % |
| Spray | 5 | 1 % |
| Sketch | 4 | 1 % |
| MyPaint | 3 | < 1 % |
| Clone (`duplicate`) | 3 | < 1 % |
| Round marker | 1 | < 1 % |
| Bristle (`hairybrush`) | 0 among verified (#27 states it) | — |

Features across those presets:

| Feature | Presets | Notes |
|---|---|---|
| Pressure sensor | 471 | almost every preset |
| Image tips (PNG 150 / GBR 59 / SVG 3) | 212 | #16 alone has 89 PNG tips. DrawApp: one imported greyscale tip per preset |
| Texture (pattern) enabled | 76 | modes: multiply, subtract, colour dodge, colour burn, hard mix, overlay, height, linear height. DrawApp: multiply, subtract |
| Masked (dual) brush | 45 | a second tip masks the first. DrawApp: **not supported** |
| Drawing-angle sensor | 143 | DrawApp: direction sensor |
| Tilt direction / elevation | 153 / 39 | DrawApp: tilt sensors (not tested on hardware) |
| Lightness (impasto) tips | 13 (#13) | DrawApp: **not supported** |
| "Parallel" / multiply / erase composite | 6 / 1 / 5 | DrawApp: no parallel; multiply and erase yes |
| Speed sensor | 20 | DrawApp: yes |

**Conclusion for milestone 11.**
- The most popular Krita resources are mostly pixel-engine brushes. What makes them distinctive
  is: image tips, textures with several modes (often height-like), masked dual tips, and
  drawing-angle and tilt dynamics. Colour smudge comes second (blenders, oils, impasto).
- Simulated engines (bristle, MyPaint) are rare among them.
- The milestone's dry-media, ink and mixing engines therefore matter most for parity with what
  artists use. Watercolour and bristle go beyond what these resources do.
- Masked dual tips and lightness tips are the largest **missing** features. They are recorded
  below; neither is in milestone 11's required list.

## Compatibility matrix

Columns:
- **Category / intended effect:** from the post (and the presets, where verified).
- **DrawApp status:**
  - *Kind supported:* DrawApp can make this kind of mark with its own engine, without importing
    the preset;
  - *Partial:* some aspects are missing;
  - *Not supported*.
- **Difficulty:** to close the listed gaps in DrawApp — low (settings or presets only), medium (a
  bounded new feature), high (a new subsystem).

| # | Category / intended effect | Important settings and dependencies | Unique behaviour | DrawApp status (milestone 10 baseline → milestone 11 plan) | Missing in DrawApp | Difficulty |
|---|---|---|---|---|---|---|
| 1 | Ink, G-pen line art | round auto tip; pressure, speed, random sensors | width responds to speed as well as pressure | Kind supported (round tip, pressure and speed dynamics) | its exact curves (not importable) | low |
| 2 | Pencils, coloured pencils, blenders | image and auto tips; texture (multiply, subtract); colour smudge (dulling and smearing); drawing angle and tilt sensors; four patterns | soft grainy graphite that builds up; tilt shading | Partial → **dry media (stage 3)** targets tooth build-up and tilt shading | its patterns (third-party), parallel blend | medium |
| 3 | Watercolour washes | per the author: "Geometric Mean" blending for watercolour-like mixing | layered transparent washes | Partial (wet paint prototype) → **watercolour (stage 7)** | geometric-mean blend mode | high |
| 4 | Metallic effects (gold leaf, glitter, hammered metal) | 23 tips including RGBA tips; 13 textures; patterns; CC-BY-SA | colour (RGBA) tips | Not supported | RGBA tips | medium |
| 5 | Various brushes and textures | external collection | — | not assessed (no single resource) | — | — |
| 6 | Impasto, thick paint | animated RGBA tips with a baked light direction; rotation by drawing angle; colour smudge (colour rate, dulling); CC-BY-SA | thick-paint lighting from tips | Not supported | animated multi-image tips, RGBA tips, a height channel | high |
| 7 | Inking for comics and realism | pixel engine; masked dual tips (15); textures including linear height; image and SVG tips; speed, angle and tilt sensors | textured ink with dry edges | Partial → **ink (stage 4)** and dry-ink texture | masked dual tip, height texture modes, SVG tips | medium |
| 8 | Inking | tilt-sensitive per author | tilt changes the line | Kind supported for tilt dynamics (untested on hardware) | — | low |
| 9 | General painting and sketching (Deevad, CC-0) | pixel with image tips (31), colour smudge (dulling, colour rate), textures, drawing angle | painterly blocking and blending | Partial → **mixing (stage 5)** | several texture modes | medium |
| 10 | Stylised painting, effects (144 presets) | pixel, colour smudge (37), deform, filter, sketch, spray, clone | wide range including filter and deform brushes | Partial | deform, filter, clone and sketch engines | high |
| 11 | Comic production | unknown | — | not assessed | — | — |
| 12 | Personal painting set | unknown | — | not assessed | — | — |
| 13 | Painting with impasto | pixel and colour smudge; 13 lightness tips; random-per-stroke and tilt sensors; separate left-handed tilt bundle | impasto relief from lightness tips | Partial → mixing (stage 5); impasto not planned | lightness (height) tips | high |
| 14 | Impasto lighting experiments | animated RGBA tips (`.gih`), rotation by drawing angle | light direction fixed across directions | Not supported | animated tips | high |
| 15 | Manga ink (fast strokes) | unknown (paid) | — | Kind supported (ink) → stage 4 | — | — |
| 16 | Concept and illustration ("Photoshoppy") | pixel with 89 PNG tips; masked dual tips (25); textures in many modes; colour smudge blenders | dual-tip texture, Photoshop-like modes | Partial | masked dual tip, texture modes | medium |
| 17 | Cloud and "size/flow" brushes | external store | — | not assessed | — | — |
| 18 | Comics and illustration | many high-resolution tips and textures (per post) | — | Partial | multi-tip presets | — |
| 19 | Flat brushes (dry brush, rough shader, wash) | "graphics tablet exclusive functionality" | flat brush turning with the pen | Partial → **bristle flat (stage 6)** | tilt or rotation untested on hardware | medium |
| 20 | Manga line art | unknown (paid) | — | Kind supported (ink) | — | — |
| 21 | Ballpoint pen | pixel; multiply blend (per author: white fringe on transparent layers) | ink that darkens where lines cross | Kind supported (multiply blend, milestone 9) | — | low |
| 22 | Markers (Copic or alcohol style) | unknown | streaky marker overlap | Partial (multiply and build-up) | marker streak texture | medium |
| 23 | Realistic ink pen | pixel; pressure, speed, angle, random | — | Kind supported → stage 4 | exact behaviour | low |
| 24 | Hair (curly, afro) | custom tips (separate tips bundle) | curly strand stamps | Partial → **procedural hair (stage 8)** | — | medium |
| 25 | Dry gouache and acrylic "where the matter runs out" | unknown; mentions a "wet paint + mix" brush | paint running out | Partial → **mixing reservoir (stage 5), bristle depletion (stage 6)** | — | medium |
| 26 | Colour mixing (Clip Studio-like oils) | colour-rate mixing; low pressure blends, high pressure adds paint (per post) | one tool paints and blends by pressure | Partial → **mixing (stage 5)**: pressure → colour rate | — | medium |
| 27 | Bristle engine lines | Krita bristle engine (per author); ink depletion | textured aliased lines from depletion | Not supported → **bristle (stage 6)** | — | high |
| 28 | Fur, foliage, clouds, FX (400+ "cheat" brushes) | many stamp tips (per post) | stamped foliage and fur | Partial → **procedural (stage 8)** | multi-stamp presets | medium |
| 29 | Hard blocking, grass, leaves, rakes, mixer | unknown (Gumroad) | rake and grass stamps | Partial → stages 5, 8 | — | medium |
| 30 | Glitch and distortion | colour smudge ×7 | pixel displacement by smudge | Partial (smudge) | deform-like displacement | medium |

## Licences of the resources

- Only #9 (CC-0), #4 and #6 (CC-BY-SA), #18 ("free to use") and #30 ("feel free to use") state
  terms in the post.
- No resource from this list is bundled with or imported into DrawApp. Even CC-0 assets would
  need a separate review before bundling. Milestone 11 bundles **no** third-party brush assets
  (`research/m11-licenses.md`).
