#pragma once
/* -----------------------------------------------------------------------
 * RemoteSpeaker — a playback's sound through the Wii Remote's speaker.
 *
 * MPlayer's audio output (ao_gekko) gives the DSP one buffer at a time with
 * AUDIO_InitDMA; linked with --wrap, each buffer comes here first: mixed
 * to mono, brought down to the speaker's 3000 Hz and queued (in the audio
 * interrupt: no lock, a ring with one writer and one reader).  A thread
 * encodes it (the Wii Remote's 4-bit ADPCM, libogc's WPAD_EncodeData) and
 * sends 40 samples a report, 75 a second, to the Wii Remote in use.  With
 * "Wii Remote only" the buffer is then silenced before the TV gets it.
 *
 * The speaker is a small one: telephone-like sound, as in the Wii's games.
 * ----------------------------------------------------------------------- */
namespace RemoteSpeaker {

enum Mode { Off = 0, RemoteOnly = 1, Both = 2, MODE_COUNT };

void setMode(int m);
int  mode();

/* around each MPlayer run (WiiPlayer): the speaker on, the thread up;
 * then off */
void start();
void stop();

} // namespace RemoteSpeaker
