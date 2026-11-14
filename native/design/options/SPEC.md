# Options screen design (chosen by the user: "C", blue swirl)

`c_split.png` is the approved look; `c_split.html` + `common.css` + `spiral_grey.svg` are its exact source (open in a browser,
1280x720). Build the native screen to match it, scaled to the window (layout in a 1280x720 reference space, scaled uniformly
and centred, drawn at the window's real pixel resolution — never through the PS2's 512x512 frame).

## Look
- Background: diagonal gradient #0e5fb8 → #0a3b7a → #071f45; the grey/white Twinsanity spiral (`spiral_grey.svg`, two arms,
  white 18% + #9fb6d8 25%) large at top-left, turning slowly (one turn per ~90 s); a faint blue/white ring target behind it.
- Title "OPTIONS": Luckiest Guy 66px, #ff9a1f fill, 2px #3a1200 outline, 4px solid drop #3a1200. Under it "CRASH TWINSANITY",
  Fredoka 600 18px, #bcd9ff, letter-spacing 3px.
- Category tabs (left column, 250x52, 10px gap): Luckiest Guy 26px #dbe9ff on white 8% with 2px white 15% border, radius 16.
  Selected: white fill, orange (#ff8a00, 1.2px #4a1800 outline) text, 6px #ffb43a bottom lip.
  Tabs (since 1.3.0, THE LIST 64): Game, Enhancements, Display, Audio, Controls, Accessibility, Mods; seven fit the column
  (the last ends at y 606, above the footer). One word each.
- Panel: white, radius 22, 10px #ffb43a bottom lip + soft shadow, 860x420 at (360,170). Rows 48px, Fredoka 600 21px #173a6b,
  2px #eef3fa separators. Value on the right: pill (#eaf2ff, radius 20, #1b6fd0 text) between orange ‹ › chevrons.
  Selected row: #ff9a1f fill, white text, pill white 25%, chevrons white. Hover (mouse) = selected.
- Sliders (audio): chunky track (#dbe7f7, 12px, radius 8) with an orange gradient fill and a Wumpa-orange round knob; value
  number next to it.
- Description line under the panel: Fredoka 600 19px #d6e8ff.
- Section titles inside a tab (a row of their own, not selectable, no divider under them): Luckiest Guy 20px #1b6fd0 capitals,
  1px letter-spacing, low in the row near the rows they name. A note line (the Enhancements tab's warning at its top): Fredoka
  600 19px #5b7194, one line.
- Footer strip: black 45%, 58px, right-aligned prompts. Keycaps: rounded 8px, #fdfdfd→#cfd6e3, 2px #1b2340 border, 4px
  #1b2340 bottom lip, Fredoka 700 15px #1b2340; label Fredoka 600 19px white with 2px black drop. Pads show the
  Xbox/PS/Switch button art at the same size instead of keycaps.
- Fonts: Luckiest Guy and Fredoka (Google Fonts, SIL Open Font License: may be bundled; ship the OFL text with them).

## Behaviour
- Everything as the current settings screen (rows, instant apply, reset per tab, descriptions, rebinding with swap).
- Controls tab: the table (Action | Key | Alt key | Controller) as rows in the same panel style; cells are pills; the selected
  cell orange; "Press a key…" shown in the cell.
- Scroll: the panel scrolls smoothly when rows overflow, with a thin orange scrollbar.
- Animations: selection bar slides between rows (~80 ms ease-out); tab change cross-fades the panel (~120 ms).
- Mouse: hover selects, click picks/steps, chevrons clickable, drag sliders, wheel scrolls; game-art cursor on top.
- Live preview (1.3.0, THE LIST 66), on Display and Accessibility: no backdrop, the paused game (or the main menu's scene)
  behind. Title and tabs in place on a #071f45 62% shade (to x 300, fading out by 340). The panel at 0.66 of its size on the
  right (right edge 1256, top 40; 690 tall in its own pixels: 13 rows). The description under it, wrapped to its width, Fredoka
  600 17px on a #071f45 82% box (radius 12). The footer adds "H" (or the right trigger) "Hide (hold)": held, nothing is drawn.
