#pragma once
#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <string>

/* -----------------------------------------------------------------------
 * Keyboard — on-screen keyboard shared by the search and sign-in screens.
 *
 *   letters page    1 2 3 4 5 6 7 8 9 0 -
 *                   q w e r t y u i o p /
 *                   a s d f g h j k l : @
 *                   z x c v b n m , . _ '
 *                   [ Shift ][ #+= ][  Space  ][ Delete ][ Search ]
 *
 *   for an address  [ Shift ][#+=][http://][https://][ Delete ][ Next ]
 *                   (http:// and https:// put that in front of the text,
 *                   in place of the one there)
 *
 * Pointer: the highlight follows it and A types the key under it (A over
 * empty space does nothing).  D-pad: moves the highlight, A types it.
 * B deletes a character (leaves when the text is empty); - deletes too, 1
 * types a space, 2 turns to the symbols, + presses the action key.  On a
 * GameCube controller: X deletes, Y a space, R the symbols, L is Shift
 * (twice: caps lock), Z the action key.  Hints for these: "kbdel",
 * "kbspace", "kbsym" (Ui::buttonName names them per controller).
 *
 * Its size (Settings > Keyboard Size) is one for all: the screens around it
 * move up by lift() so a bigger one still fits above the bottom bar.
 * ----------------------------------------------------------------------- */
class Keyboard {
public:
    enum class Result { None, Enter, Cancel };

    /* Top-left corner of the keys and the action key's label. */
    void setOrigin(float x, float y)    { x0 = x; y0 = y; }
    void setEnterLabel(const char* s)   { enterLabel = s; }
    void setUrlKeys(bool on)            { urlKeys = on; }   /* http:// and https:// for Space */
    void setPasswordMode(bool on, bool show = false) { passwordMode = on; pwShown = show; }
    bool isPasswordShown() const        { return pwShown; }
    void togglePasswordShown()          { pwShown = !pwShown; }
    void reset();

    /* Text field bounds (screen coords) so Keyboard can handle clicks on it */
    void setTextField(float x, float y, float w, float h) {
        tfX = x; tfY = y; tfW = w; tfH = h; hasTextField = true;
    }
    void clearTextField() { hasTextField = false; }
    bool isTextFieldFocused() const { return selRow == -1; }
    void setTextFieldFocused(bool f) { selRow = f ? -1 : 0; }

    int  cursorPos() const { return cursor; }
    void setCursorPos(int pos);

    /* Helpers for text field rendering and click calculation */
    int  calcScrollOffset(const std::string& text, int maxW, int fontSize) const;
    static int charIndexAt(const std::string& text, int fontSize, float clickX);

    /* Edits text (at most maxLen bytes). */
    Result update(const ir_t& ir, std::string& text, size_t maxLen = 128);
    void   render(const ir_t& ir) const;

    static const int COLS = 11, ROWS = 5;
    /* Normal (46 x 40 a key), Large, Larger */
    enum Size { SizeNormal = 0, SizeLarge = 1, SizeLarger = 2, SIZE_COUNT };
    static void setSize(int s);
    static int  size();
    static float cellW();
    static float cellH();
    float width()  const { return COLS * cellW(); }
    float height() const { return ROWS * cellH(); }
    /* how much taller than Normal: what the screen around it moves up */
    float lift()   const { return height() - ROWS * 40.0f; }

private:
    enum Special { K_NONE = 0, K_SHIFT, K_SYM, K_SPACE, K_DEL, K_ENTER, K_HTTP, K_HTTPS, K_LEFT, K_RIGHT, K_PW_TOGGLE };
    struct Key { int row, col, span; char ch; Special sp; };

    float       x0 = 67, y0 = 140;
    const char* enterLabel = "OK";
    bool  urlKeys = false;
    bool  passwordMode = false;
    bool  pwShown = false;
    int   page  = 0;          /* 0 letters, 1 symbols         */
    int   shift = 0;          /* 0 off, 1 next letter, 2 lock */
    int   selRow = 1, selCol = 0;
    int   cursor = -1;        /* -1: at the end of text */
    bool  hasTextField = false;
    float tfX = 0, tfY = 0, tfW = 0, tfH = 0;
    unsigned long long lastShiftMs = 0;
    mutable unsigned long long flashMs = 0;   /* key press feedback */
    int   flashRow = -1, flashCol = -1;

    Key  keyAt(int row, int col) const;               /* key covering a cell */
    Key  bottomKey(Special sp) const;                 /* sp on the bottom row (sp K_NONE: not there) */
    bool keyUnder(const ir_t& ir, int& row, int& col) const;
    Result press(const Key& k, std::string& text, size_t maxLen);
    const char* label(const Key& k, char* buf) const;
};
