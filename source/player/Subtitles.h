#pragma once

#include <string>
#include <vector>

struct MediaStream;

/* -----------------------------------------------------------------------
 * Subtitles — a text track (SRT, as Jellyfin converts any text subtitles
 * to), drawn by WiiFin over the picture instead of burned in by the
 * server: the video can then play as it is (direct play), and the server
 * has no picture to re-encode.
 * ----------------------------------------------------------------------- */
class Subtitles {
public:
    struct Cue {
        float from = 0.0f, to = 0.0f;   /* seconds into the item */
        std::string text;               /* lines separated by '\n', tags removed */
    };

    /* Replaces the track with an SRT file's cues; false when none parsed. */
    bool load(const std::string& srt);
    void clear() { cues.clear(); last = 0; }
    bool empty() const { return cues.empty(); }

    /* The cue shown at secs, nullptr between cues. */
    const Cue* at(float secs);

    /* Settings: whether subtitles are on by default, and preferred language */
    static bool defaultEnabled();
    static void setDefaultEnabled(bool on);

    static const std::string& preferredLanguage();
    static void setPreferredLanguage(const std::string& lang);
    static int preferredLanguageIndex();
    static void setPreferredLanguageIndex(int idx);
    static void nextPreferredLanguage();

    static int languageCount();
    static const char* languageCode(int idx);
    static const char* languageName(int idx);

    /* Selects subtitle track index based on setting and streams (-1 = off).
     * Returns -1 if defaultEnabled() is false. */
    static int selectTrack(const std::vector<MediaStream>& subs, int defaultSubIndex);

    /* Finds the best matching subtitle track index according to preferredLanguage
     * (or defaultSubIndex), regardless of defaultEnabled(). Returns -1 if subs empty. */
    static int findTrack(const std::vector<MediaStream>& subs, int defaultSubIndex);

private:
    std::vector<Cue> cues;
    size_t last = 0;   /* where the previous lookup ended: playback moves on */
};
