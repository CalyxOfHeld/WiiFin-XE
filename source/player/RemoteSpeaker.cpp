#include "RemoteSpeaker.h"
#include "WiiPlayer.h"
#include "../input/Input.h"
#include <gccore.h>
#include <ogc/audio.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>
#include <string.h>
#include <unistd.h>

namespace {

const int RATE    = 3000;      /* the speaker's samples a second (libogc's setting) */
const int PACKET  = 40;        /* samples a report: 20 bytes of 4-bit ADPCM */
const int RING    = 4096;      /* 1.4 s */

volatile int s_mode = RemoteSpeaker::Off;
volatile bool s_running = false;     /* MPlayer playing: buffers taken */

/* 3000 Hz mono, written in the audio interrupt, read by the thread */
s16 s_ring[RING];
volatile u32 s_head = 0, s_tail = 0;

/* the downsampling: a mean over each 1/3000 s */
float s_acc = 0.0f, s_phase = 0.0f;
int   s_count = 0;

lwp_t s_thread = LWP_THREAD_NULL;
volatile bool s_quit = false;
int   s_chan = -1;                   /* the Wii Remote with its speaker on */

void feed(const s16* pcm, u32 frames)
{
    const float step = RATE / (AUDIO_GetDSPSampleRate() == AI_SAMPLERATE_32KHZ ? 32000.0f : 48000.0f);
    const float vol = wii_player_muted() ? 0.0f : wii_player_volume() / 100.0f;
    for (u32 i = 0; i < frames; ++i) {
        s_acc += (pcm[i * 2] + pcm[i * 2 + 1]) * 0.5f;
        ++s_count;
        s_phase += step;
        if (s_phase < 1.0f) continue;
        s_phase -= 1.0f;
        float v = s_acc / s_count * vol;
        s_acc = 0.0f;
        s_count = 0;
        v = v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v;
        const u32 next = (s_head + 1) % RING;
        if (next == s_tail) return;            /* full: the thread is behind */
        s_ring[s_head] = (s16)v;
        s_head = next;
    }
}

void speakerOn(int chan)
{
    if (s_chan == chan) return;
    if (s_chan >= 0) WPAD_ControlSpeaker(s_chan, 0);
    s_chan = chan;
    if (chan >= 0) {
        WPAD_ControlSpeaker(chan, 1);
        SYS_Report("[Speaker] Wii Remote %d on\n", chan + 1);
    }
}

/* the Wii Remote pointed with last, or the first one there */
int remoteInUse()
{
    u32 type;
    const int p = Input::pointerChan();
    if (p >= 0 && p < 4 && WPAD_Probe(p, &type) == WPAD_ERR_NONE) return p;
    for (int ch = 0; ch < 4; ++ch) if (WPAD_Probe(ch, &type) == WPAD_ERR_NONE) return ch;
    return -1;
}

void* run(void*)
{
    WPADEncStatus enc;
    bool first = true;
    u64 start = 0, sent = 0;          /* reports since start */
    u32 lastCheck = 0;
    while (!s_quit) {
        const u64 now = ticks_to_millisecs(gettime());
        /* the remote: checked twice a second (one reconnects, another is used) */
        if ((u32)now - lastCheck > 500) {
            lastCheck = (u32)now;
            const int ch = remoteInUse();
            if (ch != s_chan || (ch >= 0 && !WPAD_IsSpeakerEnabled(ch))) {
                if (ch == s_chan && ch >= 0) s_chan = -1;   /* back from sleep: on again */
                speakerOn(ch);
                first = true;
            }
        }
        const u32 have = (s_head + RING - s_tail) % RING;
        if (have < (u32)PACKET || s_chan < 0) {
            start = 0;                 /* nothing to say: the pace starts over */
            usleep(4000);
            continue;
        }
        if (!start) { start = now; sent = 0; }
        /* 75 reports a second, two ahead at most (the remote's own buffer) */
        if (sent >= (now - start) * RATE / PACKET / 1000 + 2) { usleep(2000); continue; }
        /* fallen far behind (the thread held up): the old sound dropped */
        if (have > (u32)RATE / 4) s_tail = (s_head + RING - PACKET * 2) % RING;
        s16 pcm[PACKET];
        for (int i = 0; i < PACKET; ++i) pcm[i] = s_ring[(s_tail + i) % RING];
        s_tail = (s_tail + PACKET) % RING;
        u8 out[PACKET / 2];
        WPAD_EncodeData(&enc, first ? WPAD_ENC_FIRST : WPAD_ENC_CONT, pcm, PACKET, out);
        first = false;
        WPAD_SendStreamData(s_chan, out, sizeof(out));
        ++sent;
    }
    return nullptr;
}

} // namespace

void RemoteSpeaker::setMode(int m) { s_mode = m >= 0 && m < MODE_COUNT ? m : Off; }
int  RemoteSpeaker::mode() { return s_mode; }

void RemoteSpeaker::start()
{
    if (s_mode == Off || s_thread != LWP_THREAD_NULL) return;
    s_head = 0;
    s_tail = 0;
    s_acc = s_phase = 0.0f;
    s_count = 0;
    s_quit = false;
    s_chan = -1;
    s_running = true;
    LWP_CreateThread(&s_thread, run, nullptr, nullptr, 16 * 1024, 70);
}

void RemoteSpeaker::stop()
{
    s_running = false;
    if (s_thread == LWP_THREAD_NULL) return;
    s_quit = true;
    LWP_JoinThread(s_thread, nullptr);
    s_thread = LWP_THREAD_NULL;
    speakerOn(-1);
    SYS_Report("[Speaker] off\n");
}

/* every buffer the DSP is given (MPlayer's, the menus' sounds): during a
 * playback with the speaker on, its sound to the Wii Remote first */
extern "C" {
void __real_AUDIO_InitDMA(u32 startaddr, u32 len);
void __wrap_AUDIO_InitDMA(u32 startaddr, u32 len)
{
    if (s_running && startaddr && len >= 4) {
        s16* pcm = (s16*)MEM_PHYSICAL_TO_K0(startaddr & 0x1FFFFFFF);
        feed(pcm, len / 4);
        if (s_mode == RemoteSpeaker::RemoteOnly) {   /* nothing on the TV */
            memset(pcm, 0, len);
            DCFlushRange(pcm, len);
        }
    }
    __real_AUDIO_InitDMA(startaddr, len);
}
}
