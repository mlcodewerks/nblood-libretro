// Pull-driven MultiVoc PCM and timestamped MIDI output. GPL-2.0-or-later.
#include "bridge.h"
#include "drivers.h"
#include "midi.h"
#include "_midi.h"
#include <algorithm>
#include <cstring>

int ASS_PCMSoundDriver = ASS_SDL, ASS_MIDISoundDriver = ASS_SDL, ASS_EMIDICard = -1;
static char *mixBuffer;
static int mixSize, mixCount, mixCurrent, mixUsed;
static void (*mixCallback)();
static bool midiPlaying;
static double midiTicksPerSecond = 192, midiAccumulator;

int SoundDriver_IsPCMSupported(int d) { return d >= 0 && d < ASS_NumSoundCards; }
int SoundDriver_IsMIDISupported(int d) { return d >= 0 && d < ASS_NumSoundCards; }
const char *SoundDriver_GetName(int) { return "Libretro"; }
int SoundDriver_PCM_GetError() { return 0; }
int SoundDriver_MIDI_GetError() { return 0; }
const char *SoundDriver_PCM_ErrorString(int) { return "Libretro PCM"; }
const char *SoundDriver_MIDI_ErrorString(int) { return "Libretro MIDI"; }
int SoundDriver_PCM_Init(int *rate, int *channels, void *) { *rate = 48000; *channels = 2; return 0; }
void SoundDriver_PCM_StopPlayback() { mixBuffer = nullptr; mixCallback = nullptr; }
void SoundDriver_PCM_Shutdown() { SoundDriver_PCM_StopPlayback(); }
int SoundDriver_PCM_BeginPlayback(char *buf, int size, int count, void (*cb)())
{
    mixBuffer = buf; mixSize = size; mixCount = count; mixCallback = cb;
    mixCurrent = 0; mixUsed = size; return 0;
}
void SoundDriver_PCM_Lock() {}
void SoundDriver_PCM_Unlock() {}
void retro_audio_render(int16_t *samples, size_t frames)
{
    char *out = reinterpret_cast<char *>(samples);
    size_t remaining = frames * 4;
    std::memset(out, 0, remaining);
    if (!mixBuffer || !mixCallback) return;
    while (remaining)
    {
        if (mixUsed == mixSize) { mixCallback(); mixUsed = 0; mixCurrent = (mixCurrent + 1) % mixCount; }
        size_t n = std::min(remaining, size_t(mixSize - mixUsed));
        std::memcpy(out, mixBuffer + mixCurrent * mixSize + mixUsed, n);
        mixUsed += n; remaining -= n; out += n;
    }
}
static void message(int status, int channel, int a, int b = -1)
{
    retro_midi_write(status | (channel & 15)); retro_midi_write(a & 127);
    if (b >= 0) retro_midi_write(b & 127);
}
static void noteOff(int c, int k, int v) { message(0x80,c,k,v); }
static void noteOn(int c, int k, int v) { message(0x90,c,k,v); }
static void poly(int c, int k, int p) { message(0xa0,c,k,p); }
static void control(int c, int n, int v) { message(0xb0,c,n,v); }
static void program(int c, int p) { message(0xc0,c,p); }
static void pressure(int c, int p) { message(0xd0,c,p); }
static void bend(int c, int l, int h) { message(0xe0,c,l,h); }
static void sysex(const unsigned char *data, int length)
{
    retro_midi_write(0xf0);
    for (int i = 0; i < length; ++i) retro_midi_write(data[i]);
    if (!length || data[length-1] != 0xf7) retro_midi_write(0xf7);
}
int SoundDriver_MIDI_Init(midifuncs *f)
{
    *f = {}; f->NoteOff = noteOff; f->NoteOn = noteOn; f->PolyAftertouch = poly;
    f->ControlChange = control; f->ProgramChange = program;
    f->ChannelAftertouch = pressure; f->PitchBend = bend; f->SysEx = sysex;
    midiAccumulator = 0; return 0;
}
void SoundDriver_MIDI_Shutdown() { midiPlaying = false; }
int SoundDriver_MIDI_StartPlayback() { midiAccumulator = 0; midiPlaying = true; return 0; }
void SoundDriver_MIDI_HaltPlayback() { midiPlaying = false; }
void SoundDriver_MIDI_SetTempo(int tempo, int division) { midiTicksPerSecond = double(tempo) * division / 60.0; }
void SoundDriver_MIDI_Lock() {}
void SoundDriver_MIDI_Unlock() {}
int SoundDriver_MIDI_GetCardType() { return EMIDI_GeneralMIDI; }
void SoundDriver_MIDI_Service() {}
void retro_midi_advance(unsigned usec)
{
    if (!midiPlaying) return;
    // Service one tick at a time so tempo events affect the following tick.
    double remaining = usec / 1000000.0;
    uint64_t start = retro_time_usec() - usec;
    while (midiPlaying && midiTicksPerSecond > 0)
    {
        double untilTick = (1.0 - midiAccumulator) / midiTicksPerSecond;
        if (remaining < untilTick) { midiAccumulator += remaining * midiTicksPerSecond; break; }
        remaining -= untilTick; midiAccumulator = 0;
        retro_midi_set_time(start + usec - uint64_t(remaining * 1000000.0));
        MIDI_ServiceRoutine();
    }
}
