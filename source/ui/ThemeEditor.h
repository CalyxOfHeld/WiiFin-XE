#pragma once
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <string>
#include "Ui.h"
#include "Keyboard.h"

/* Customize Theme (Settings > View): the theme in use, its ten main
 * colours listed on the left, the highlighted one's picker on the right:
 * a square of shades (saturation and brightness) and a hue slide, red,
 * green and blue slides, and a field that takes #RRGGBB or r,g,b.  The
 * whole screen shows the colours as they change.  Save As writes it to
 * themes/ under a name; a built-in theme is never changed. */
class ThemeEditor {
public:
    void open();
    /* true: closed (the theme in use: the saved one, else as it was) */
    bool update(ir_t& ir);
    void render(ir_t& ir);

private:
    enum { ROW_SAVE = Ui::THEME_COLOR_COUNT, ROW_RESET, ROW_COUNT };
    enum Part { P_SQUARE, P_HUE, P_RED, P_GREEN, P_BLUE, P_TEXT, P_COUNT };

    Ui::CustomTheme work, opened;   /* being edited; as it was opened */
    Ui::Palette     pal;            /* work's, drawn by everything meanwhile */
    bool  dirty    = false;

    int   sel      = 0;             /* the list: a colour, Save As, Reset */
    float rowAnim[ROW_COUNT] = {};
    int   lastHover = -1;
    bool  inPicker = false;
    int   part     = P_SQUARE;
    bool  grabbed  = false;         /* the square: the D-pad moves its ring */
    int   dragging = -1;            /* the part the pointer holds with A */
    float h = 0, s = 0, v = 0;      /* the colour as hue 0..360, 0..1, 0..1 */
    u32   before   = 0;             /* the colour when the picker was entered */
    u64   heldSince = 0;            /* D-pad held: steps grow */

    /* the keyboard: a colour typed (field), or the name to save under */
    enum class Typing { None, Color, Name };
    Typing      typing = Typing::None;
    std::string draft;
    Keyboard    kb;

    /* B with changes not saved: Save As, or leave them */
    bool  asking = false;
    int   askSel = 0;

    u32  color() const;              /* the highlighted colour, as the theme has it */
    void setColor(u32 c, bool keepHsv);
    void fromColor();                /* h, s, v from the highlighted colour */
    void applyHsv();
    int  heldStep(u32 minus, u32 plus);
    bool updatePicker(ir_t& ir);
    void startSave();
    void close();
};
