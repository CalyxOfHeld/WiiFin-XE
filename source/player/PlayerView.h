#pragma once

#include <grrlib.h>
#include <wiiuse/wpad.h>
#include <string>
#include <vector>
#include <map>
#include "../jellyfin/JellyfinClient.h"

class Trickplay;
class Subtitles;

/* -----------------------------------------------------------------------
 * PlayerViewContext — what is playing; refreshed by App for each stream.
 * ----------------------------------------------------------------------- */
struct PlayerViewContext {
    std::string title;                      /* shown in the top bar           */
    std::vector<JellyfinEpisode> episodes;  /* season episodes; empty = movie */
    int episodeIdx = 0;

    std::vector<MediaStream> audioStreams;
    std::vector<MediaStream> subStreams;
    int currentAudio = 0;    /* MediaStream::index of the playing audio track */
    int currentSub   = -1;   /* MediaStream::index of the subtitle, -1 = off  */

    IntroInfo intro;
    /* where its chapters start (s): |< and >| go from one to the next */
    std::vector<float> chapters;

    /* The transcode starts at streamOrigin seconds into the item and MPlayer
     * skips its first startSkip seconds, so the absolute position is
     * streamOrigin + max(wii_player_time_pos(), startSkip), unless that
     * skip was missed (wii_player_ss_missed). */
    float streamOrigin = 0.0f;
    float startSkip    = 0.0f;  /* seconds MPlayer discards at start (-ss) */
    float runtime      = 0.0f;  /* item length in seconds, 0 = unknown */
};

/* -----------------------------------------------------------------------
 * PlayerView — the in-player UI, drawn with GRRLIB over the video.
 *
 * Main loop, once per frame:
 *     Input::update(); WPAD_IR(...);
 *     Action a = view.update(Input::rawDown(), ir);
 *     view.render(ir);
 *     GRRLIB_Render();
 *     VideoSurface::endFrame();
 * ----------------------------------------------------------------------- */
class PlayerView {
public:
    enum class Action {
        None,
        Back,     /* B: leave the player                           */
        Next,     /* next episode                                  */
        Prev,     /* previous episode                              */
        Audio,    /* switch to chosenAudio()                       */
        Sub,      /* switch to chosenSub()                         */
        SeekTo,   /* restart the stream at seekTarget() (absolute s) */
        Home,     /* HOME button                                   */
    };

    PlayerView(GRRLIB_ttfFont* font, GRRLIB_texImg* cursor, GRRLIB_texImg* ring);

    /* the settings: B asks before the video stops; + / - and |< >| go
     * to the next or previous episode (off: |< >| only the chapters) */
    static bool askBeforeStop;
    static bool askBeforeNext;
    static bool episodeButtons;
    /* the Skip button over intros, recaps and credits (their times from
     * the server: Intro Skipper); intros and recaps only once this many
     * episodes of the series were watched (0: from the first) */
    static bool skipButton;
    static int  skipIntroAfter;
    static const int SKIP_AFTER_MAX = 10;
    /* a series' own choice over those two (2 in its episodes), the open
     * profile's: SKIP_OFF no button, 0 from the first episode, n once n
     * were watched; a series not in it follows the settings */
    static const int SKIP_OFF = -1;
    static std::map<std::string, int> seriesSkip;
    static bool seriesSkipChanged;   /* to be saved with the profile */
    static void skipFor(const std::string& seriesId, bool& on, int& after);
    static void loadSeriesSkip(const std::string& s);   /* "id:n,id:n" */
    static std::string seriesSkipString();

    void setContext(const PlayerViewContext& c);

    /* Busy message shown with the spinner over the last frame (stream
     * switch, stopping...), in English (N_): translated when drawn.
     * Empty string = normal playback UI. */
    void setBusy(const std::string& msg) { busyMsg = msg; }

    /* Rebuffering progress (0-100) while App holds playback paused to refill
     * the cache, -1 otherwise. */
    void setRebuffering(int percent) { rebufPercent = percent; }

    /* Thumbnails shown over the seek bar while seeking; nullptr: none. */
    void setTrickplay(Trickplay* t) { trickplay = t; }

    /* Text subtitles drawn over the picture (not burned in); nullptr: none.
     * currentSub: the track the picker shows as current. */
    void setSubtitles(Subtitles* s, int currentSub) { subtitles = s; ctx.currentSub = currentSub; pickSub = currentSub; }

    Action update(u32 btnsDown, const ir_t& ir);
    void   render(const ir_t& ir);

    int   chosenAudio() const { return pickAudio; }
    int   chosenSub()   const { return pickSub; }
    float seekTarget()  const { return seekTo; }
    /* The seek is done by MPlayer in the same stream (direct play): the
     * target shows until MPlayer is there. */
    void  seekInPlace();

    /* Absolute playback position in seconds. */
    float position() const;

    /* True while playback is stuck (not paused, position not moving). */
    bool  buffering() const;

    void toast(const std::string& msg, int ms = 2000);
    void showVolume(int vol, bool muted = false);

    /* Prompt user to Continue or start From Start when an episode has a checkpoint */
    long long askResume(ir_t& ir, long long resumeTicks, const std::string& epTitle = "");

    /* The server's remote control (Remote): a seek to an absolute position
     * or by delta s, as the D-pad's (merged, then Action::SeekTo); a track
     * switch (Jellyfin stream index, -1 = no subtitles), as the picker's:
     * the next update() returns Action::Audio / Sub. */
    void remoteSeek(float secs);
    void remoteNudge(float delta) { nudgeSeek(delta); }
    void remoteTrack(bool audio, int index);

private:
    enum class Panel { None, Audio, Sub };

    GRRLIB_ttfFont* font;
    GRRLIB_texImg*  cursorTex;
    GRRLIB_texImg*  ringTex;

    PlayerViewContext ctx;
    std::string busyMsg;
    int         rebufPercent = -1;
    Trickplay*  trickplay = nullptr;
    Subtitles*  subtitles = nullptr;
    void drawSubtitle(float bottom);

    Panel panel        = Panel::None;
    int   panelSel     = 0;
    int   panelTop     = 0;
    u64   controlsUntil = 0;   /* ms timestamp; controls visible until then */
    bool  hudHidden     = false; /* Y pressed: HUD hidden until next button */
    bool  firstPicture  = false; /* the first picture shown: the controls go soon after */
    u64   holdSince = 0, holdLast = 0;   /* Left / Right held: seeking on */
    float controlsAlpha = 0.0f;
    std::string toastMsg;
    u64   toastUntil   = 0;
    float lastIrX = -1.0f, lastIrY = -1.0f;   /* where the pointer rested */
    u64   irAnchorMs = 0;
    unsigned skippedSegments = 0;   /* bit i: segment i skipped (or not wanted) */

    /* Stall detection: the position stops moving while not paused when
     * MPlayer's cache runs dry (slow Wi-Fi or server). */
    float lastSeenPos     = -1.0f;
    u64   lastPosChangeMs = 0;

    int   pickAudio = 0, pickSub = -1;
    float seekTo    = 0.0f;

    /* Left/Right presses accumulate into one seek, committed after a pause */
    bool  seekInFlight  = false;   /* SeekTo returned, new stream not started */
    bool  seekInPlaceSince = false; /* ... or MPlayer seeking in this one */
    float seekInPlaceFrom  = 0.0f;  /* MPlayer's position when it was asked */
    u64   seekInPlaceAt    = 0;
    u64   loadingSince     = 0;     /* the loading indicator went on, 0 = off */
    Action injected     = Action::None;   /* remoteTrack */
    bool  seekPending   = false;
    float seekPendingTo = 0.0f;
    u64   seekCommitAt  = 0;

    /* Hit-testing results for the current frame */
    int   hoverButton = -1;    /* 0..4 = transport buttons, 5 = zoom */
    int   focusedButton = 2;   /* controller focus: 0..4 = transport, 5 = zoom, 6 = intro, 7 = the bar */
    int   focusSegment  = -1;  /* the segment whose skip button last took the focus */
    int   volumeLevel = 50;
    bool  volumeMuted = false;
    u64   volumeUntil = 0;
    u64   volHoldSince = 0, volHoldLast = 0;
    bool  hoverBar    = false;
    int   hoverRow    = -1;    /* panel row */
    int   prevHoverRow = -1;   /* hover only moves the selection when it changes */
    bool  hoverIntro  = false;
    /* B with Ask Before Stopping: Stop / Keep watching, over the video */
    bool  stopAsk     = false;
    int   stopSel     = 0;     /* 0 = Stop, 1 = Keep watching */
    int   stopHover   = -1;
    Action updateStopAsk(u32 down, const ir_t& ir);
    void   drawStopAsk();

    /* Next with Ask Before Next: Skip / Keep watching */
    bool  nextAsk     = false;
    int   nextSel     = 0;     /* 0 = Skip, 1 = Keep watching */
    int   nextDir     = 1;     /* +1 the next episode, -1 the previous */
    int   nextHover   = -1;
    Action updateNextAsk(u32 down, const ir_t& ir);
    void   drawNextAsk();

    float duration() const;
    bool  introVisible() const { return activeSegment() >= 0; }
    /* the segment being played that offers its button (ctx.intro), -1 none */
    int   activeSegment() const;
    const char* segmentLabel(int i) const;
    bool  controlsVisible() const;
    void  nudgeSeek(float delta, int waitMs = 400);
    float displayPosition() const;
    void  showControls(int ms = 5000);
    bool  chapterJump(int dir);   /* |< >| through the chapters, when there are */
    void  openPanel(Panel p);
    int   panelCount() const;
    std::string panelLabel(int row) const;
    bool  panelRowIsCurrent(int row) const;
    void  hitTest(const ir_t& ir);
    void  toggleZoom();          /* Fit <-> Fill, kept for the next videos */
    Action activatePanelRow(int row);

    void drawSpinner(float cx, float cy);
    std::string fitText(const std::string& s, int size, int maxW);
};
