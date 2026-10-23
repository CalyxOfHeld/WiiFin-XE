# Themes of your own

WiiFin-XE keeps its built-in themes (Light, Dark, Flix, Luhu, Sushi) and can use themes you make.

## Making one on the Wii

Settings > View > **Customize Theme** opens the theme in use. Pick a colour on the left; on the right:

- the **square**: across for how much colour, down for how dark (point and hold A, or A then the D-pad);
- the **Hue**, **Red**, **Green** and **Blue** slides (Left/Right, or point and hold A);
- **Code**: type `#AA5CC3` or `170, 92, 195`.

The whole screen shows the colours as they change. **-** puts back the colour as it was before you started changing it. **Save As...** saves it under a name; a built-in theme is never changed. The Theme setting then lists it after the built-in ones.

## The files

Each theme is a text file in `sd:/apps/WiiFin/themes/`, so you can copy, share or edit it on a PC:

```
name = Midnight
base = Dark
background     = #101820
card           = #22303F
accent         = #AA5CC3
text           = 240, 240, 240
```

- `base`: a built-in theme. Any colour the file leaves out comes from it, and so does the layout (`Flix`: the band at the top, drawn in your accent).
- Colours: `#RRGGBB`, `#RRGGBBAA`, `RRGGBB`, `r, g, b` or `rgb(r, g, b)`.
- Lines starting with `#` are comments.

The main colours, each with the shades that go with it:

| Key | What it colours |
|---|---|
| `background` | the screen behind everything |
| `card` | buttons, tiles, panels, list rows |
| `border` | outlines of cards, bars and text fields |
| `text` | titles and labels |
| `text_dim` | details, descriptions, hints |
| `accent` | the ring around what is selected, setting values (and the focus glow) |
| `highlight` | the label of what is selected |
| `text_on_accent` | words drawn on the accent colour |
| `bar` | the bar along the bottom |
| `field` | where you type |

To set one exact shade instead, use the palette's own names: `bg_top`, `bg_bottom`, `stripe`, `card_top`, `card_bottom`, `card_border`, `accent_dark`, `glow`, `shadow`, `bar_top`, `bar_bottom`, `bar_border`, `field_border`, `danger`, `ok`, `dim`. They apply after the main colours.
