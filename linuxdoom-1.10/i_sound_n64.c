// N64 sound backend using libdragon mixer (RSP microcode accelerated).

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "doomdef.h"
#include "doomstat.h"
#include "i_sound.h"
#include "i_system.h"
#include "sounds.h"
#include "w_wad.h"
#include "z_zone.h"

#define N64_AUDIO_FREQUENCY     22050
#define N64_AUDIO_NUM_BUFFERS   4
#define N64_DEFAULT_SFX_RATE    11025
#define N64_MUSIC_CHANNEL_BUDGET 16
#define N64_MUSIC_PATH_MAX      96

// From s_sound.c
extern int numChannels;
extern int snd_MusicVolume;

char* sndserver_filename = "sndserver";

typedef struct
{
    waveform_t wave;
    int8_t* pcm;
    int length;
    int frequency;
    boolean loaded;
    boolean owns_pcm;
} n64_sfx_t;

typedef struct
{
    int handle;
    int sfx_id;
    int start_tic;
    boolean active;
} n64_voice_t;

typedef enum
{
    N64_MUSIC_FMT_NONE = 0,
    N64_MUSIC_FMT_XM64,
    N64_MUSIC_FMT_YM64,
    N64_MUSIC_FMT_MUS
} n64_music_format_t;

typedef struct
{
    int handle;
    int lumpnum;
    int channels;
    int mus_data_len;
    n64_music_format_t format;
    const void *mus_data;
    char lump_name[9];
    char asset_path[N64_MUSIC_PATH_MAX];
    boolean registered;
    boolean opened;
    boolean playing;
    boolean paused;
    boolean looping;
    xm64player_t xm;
    ym64player_t ym;
} n64_music_track_t;

static n64_sfx_t sfx_cache[NUMSFX];
static n64_voice_t voices[MIXER_MAX_CHANNELS];
static n64_music_track_t music_track;

static int voice_count;
static int mixer_channel_count;
static int music_first_channel;
static int music_channel_count;
static int next_handle = 1;
static int next_music_handle = 1;
static boolean sound_initialized;
static boolean music_initialized;
static boolean missing_music_warning_printed;

static uint16_t N64_ReadLE16(const uint8_t* p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t N64_ReadLE32(const uint8_t* p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int N64_NormalizeVolume(int volume)
{
    if (volume < 0)
        return 0;

    // Doom defaults are often 0..15, while mixer gain math expects 0..127.
    if (volume <= 15)
        return (volume * 127 + 7) / 15;

    if (volume > 127)
        return 127;

    return volume;
}

static void N64_SfxWaveRead(void* ctx, samplebuffer_t* sbuf, int wpos, int wlen, bool seeking)
{
    n64_sfx_t* sfx = (n64_sfx_t*)ctx;
    int available;
    int8_t* dst;

    (void)seeking;

    if (!sfx || !sfx->pcm || wlen <= 0)
        return;

    if (wpos < 0)
        wpos = 0;

    if (wpos >= sfx->length)
        return;

    available = sfx->length - wpos;
    if (available > wlen)
        available = wlen;

    dst = (int8_t*)samplebuffer_append(sbuf, available);
    memcpy(dst, sfx->pcm + wpos, (size_t)available);
}

static void N64_SetupWaveform(n64_sfx_t* slot, const char* name)
{
    memset(&slot->wave, 0, sizeof(slot->wave));
    slot->wave.name = name;
    slot->wave.bits = 8;
    slot->wave.channels = 1;
    slot->wave.frequency = (float)slot->frequency;
    slot->wave.len = slot->length;
    slot->wave.loop_len = 0;
    slot->wave.start = NULL;
    slot->wave.read = N64_SfxWaveRead;
    slot->wave.ctx = slot;
    slot->wave.state_size = 0;
}

static int N64_GetSfxLumpByName(const char* sfx_name)
{
    char lump_name[20];

    sprintf(lump_name, "ds%s", sfx_name);

    if (W_CheckNumForName(lump_name) == -1)
        return W_GetNumForName("dspistol");

    return W_GetNumForName(lump_name);
}

static int N64_ResolveRootSfx(int sfx_id)
{
    int root;
    int safety;

    root = sfx_id;
    safety = 0;

    while (S_sfx[root].link && safety < NUMSFX)
    {
        int next = (int)(S_sfx[root].link - S_sfx);
        if (next <= 0 || next >= NUMSFX || next == root)
            break;

        root = next;
        safety++;
    }

    return root;
}

static void N64_LoadRootSfx(int sfx_id)
{
    n64_sfx_t* slot;
    int lump;
    int raw_len;
    uint8_t* raw;
    int rate;
    int count;
    int i;
    int8_t* pcm;

    slot = &sfx_cache[sfx_id];
    if (slot->loaded)
        return;

    lump = N64_GetSfxLumpByName(S_sfx[sfx_id].name);
    raw_len = W_LumpLength(lump);
    raw = (uint8_t*)W_CacheLumpNum(lump, PU_STATIC);

    rate = N64_DEFAULT_SFX_RATE;
    count = 0;

    if (raw_len > 8)
    {
        rate = (int)N64_ReadLE16(raw + 2);
        count = (int)N64_ReadLE32(raw + 4);

        if (count <= 0 || count > raw_len - 8)
            count = raw_len - 8;
    }
    else if (raw_len > 0)
    {
        count = raw_len;
    }

    pcm = NULL;
    if (count > 0)
    {
        pcm = (int8_t*)malloc((size_t)count);
        if (!pcm)
            I_Error("I_InitSound: out of memory for SFX %s (%d bytes)",
                    S_sfx[sfx_id].name, count);

        if (raw_len > 8)
        {
            for (i = 0; i < count; i++)
                pcm[i] = (int8_t)((int)raw[8 + i] - 128);
        }
        else
        {
            for (i = 0; i < count; i++)
                pcm[i] = (int8_t)((int)raw[i] - 128);
        }
    }

    Z_Free(raw);

    slot->pcm = pcm;
    slot->length = count;
    slot->frequency = (rate > 0) ? rate : N64_DEFAULT_SFX_RATE;
    slot->loaded = true;
    slot->owns_pcm = true;

    S_sfx[sfx_id].lumpnum = lump;
    S_sfx[sfx_id].data = slot->pcm;

    N64_SetupWaveform(slot, S_sfx[sfx_id].name);
}

static void N64_AliasSfx(int sfx_id, int root_sfx_id)
{
    n64_sfx_t* dst;
    n64_sfx_t* src;

    dst = &sfx_cache[sfx_id];
    src = &sfx_cache[root_sfx_id];

    dst->pcm = src->pcm;
    dst->length = src->length;
    dst->frequency = src->frequency;
    dst->loaded = true;
    dst->owns_pcm = false;

    S_sfx[sfx_id].lumpnum = S_sfx[root_sfx_id].lumpnum;
    S_sfx[sfx_id].data = dst->pcm;

    N64_SetupWaveform(dst, S_sfx[sfx_id].name);
}

static void N64_LoadAllSfx(void)
{
    int i;

    memset(sfx_cache, 0, sizeof(sfx_cache));

    for (i = 1; i < NUMSFX; i++)
    {
        int root = N64_ResolveRootSfx(i);

        if (!sfx_cache[root].loaded)
            N64_LoadRootSfx(root);

        if (i != root)
            N64_AliasSfx(i, root);
    }
}

static void N64_ClearVoice(int voice)
{
    voices[voice].active = false;
    voices[voice].handle = 0;
    voices[voice].sfx_id = 0;
    voices[voice].start_tic = 0;
}

static int N64_FindVoiceByHandle(int handle)
{
    int i;

    for (i = 0; i < voice_count; i++)
    {
        if (voices[i].active && voices[i].handle == handle)
            return i;
    }

    return -1;
}

static int N64_AllocVoice(void)
{
    int i;
    int oldest_tic;
    int oldest_voice;

    oldest_tic = gametic;
    oldest_voice = 0;

    for (i = 0; i < voice_count; i++)
    {
        if (!voices[i].active)
            return i;

        if (!mixer_ch_playing(i))
        {
            N64_ClearVoice(i);
            return i;
        }

        if (voices[i].start_tic < oldest_tic)
        {
            oldest_tic = voices[i].start_tic;
            oldest_voice = i;
        }
    }

    return oldest_voice;
}

static float N64_PitchScale(int pitch)
{
    if (pitch < 0)
        pitch = 0;
    else if (pitch > 255)
        pitch = 255;

    return powf(2.0f, ((float)pitch - 128.0f) / 64.0f);
}

static void N64_ApplyChannelParams(int voice, int vol, int sep, int pitch)
{
    int separation;
    int left;
    int right;
    float lvol;
    float rvol;
    float freq;
    int sfx_id;

    vol = N64_NormalizeVolume(vol);

    if (sep < 0)
        sep = 0;
    else if (sep > 255)
        sep = 255;

    separation = sep + 1;
    left = vol - ((vol * separation * separation) >> 16);

    separation -= 257;
    right = vol - ((vol * separation * separation) >> 16);

    if (left < 0)
        left = 0;
    else if (left > 127)
        left = 127;

    if (right < 0)
        right = 0;
    else if (right > 127)
        right = 127;

    lvol = (float)left / 127.0f;
    rvol = (float)right / 127.0f;
    mixer_ch_set_vol(voice, lvol, rvol);

    sfx_id = voices[voice].sfx_id;
    if (sfx_id > 0 && sfx_id < NUMSFX)
    {
        freq = sfx_cache[sfx_id].wave.frequency * N64_PitchScale(pitch);
        mixer_ch_set_freq(voice, freq);
    }
}

static void N64_ResetMusicTrack(boolean close_player)
{
    if (close_player && music_track.opened)
    {
        if (music_track.playing || music_track.paused)
        {
            if (music_track.format == N64_MUSIC_FMT_XM64)
                xm64player_stop(&music_track.xm);
            else if (music_track.format == N64_MUSIC_FMT_YM64)
                ym64player_stop(&music_track.ym);
            else if (music_track.format == N64_MUSIC_FMT_MUS)
                mixer_ch_stop(music_first_channel);
        }

        if (music_track.format == N64_MUSIC_FMT_XM64)
            xm64player_close(&music_track.xm);
        else if (music_track.format == N64_MUSIC_FMT_YM64)
            ym64player_close(&music_track.ym);
    }

    memset(&music_track, 0, sizeof(music_track));
}

static int N64_FindLumpNumByData(void* data)
{
    int i;

    if (!data)
        return -1;

    for (i = 0; i < numlumps; i++)
    {
        if (lumpcache[i] == data)
            return i;
    }

    return -1;
}

static void N64_GetLumpNameVariants(int lumpnum, char* lower, char* upper)
{
    char raw[9];
    int i;

    memset(raw, 0, sizeof(raw));
    memcpy(raw, lumpinfo[lumpnum].name, 8);
    raw[8] = '\0';

    for (i = 0; i < 8; i++)
    {
        if (raw[i] == ' ')
        {
            raw[i] = '\0';
            break;
        }
    }

    for (i = 0; i < 9; i++)
    {
        unsigned char c;

        if (!raw[i])
            break;

        c = (unsigned char)raw[i];
        lower[i] = (char)tolower(c);
        upper[i] = (char)toupper(c);
    }

    lower[i] = '\0';
    upper[i] = '\0';
}

static boolean N64_FileExists(const char* path)
{
    FILE* fp;

    fp = fopen(path, "rb");
    if (!fp)
        return false;

    fclose(fp);
    return true;
}

static boolean N64_ResolveMusicTrackAsset(int lumpnum)
{
    static const char* dirs[] = { "rom:/music/", "rom:/" };
    static const char* exts[] = { ".xm64", ".XM64", ".ym64", ".YM64" };
    static const n64_music_format_t fmts[] = {
        N64_MUSIC_FMT_XM64,
        N64_MUSIC_FMT_XM64,
        N64_MUSIC_FMT_YM64,
        N64_MUSIC_FMT_YM64
    };
    char lower[9];
    char upper[9];
    const char* names[2];
    int d;
    int n;
    int e;

    if (lumpnum < 0 || lumpnum >= numlumps)
        return false;

    N64_GetLumpNameVariants(lumpnum, lower, upper);
    names[0] = lower;
    names[1] = upper;

    snprintf(music_track.lump_name,
             sizeof(music_track.lump_name),
             "%s",
             lower);

    for (d = 0; d < (int)(sizeof(dirs) / sizeof(dirs[0])); d++)
    {
        for (n = 0; n < 2; n++)
        {
            for (e = 0; e < (int)(sizeof(exts) / sizeof(exts[0])); e++)
            {
                snprintf(music_track.asset_path,
                         sizeof(music_track.asset_path),
                         "%s%s%s",
                         dirs[d],
                         names[n],
                         exts[e]);

                if (N64_FileExists(music_track.asset_path))
                {
                    music_track.format = fmts[e];
                    return true;
                }
            }
        }
    }

    music_track.asset_path[0] = '\0';
    music_track.format = N64_MUSIC_FMT_NONE;
    return false;
}

// ============================================================
// MUS soft synthesizer (reads directly from WAD lump data)
// ============================================================

#define MUS_MAX_CHANNELS  16
#define MUS_PERC_CHANNEL  15
#define MUS_TOTAL_VOICE   MUS_MAX_CHANNELS
#define MUS_PERC_SLOT     MUS_PERC_CHANNEL
#define MUS_TICK_RATE     140
#define MUS_PERC_DECAY    1764
#define MUS_DEBUG_EVENT_LOG_BUDGET 24
#define MUS_DEBUG_SILENCE_SAMPLES  (N64_AUDIO_FREQUENCY * 2)

typedef struct
{
    uint32_t phase;
    uint32_t phase_inc;
    int      perc_left;
    uint8_t  ch_vol;
    uint8_t  velocity;
    uint8_t  note;
    uint8_t  active;
    uint8_t  is_perc;
    uint8_t  _pad[3];
} mus_voice_t;

typedef struct
{
    waveform_t    wave;
    const uint8_t *score;
    int            score_len;
    int            score_pos;
    int32_t        tick_delay;
    int            tick_frac;
    uint32_t       noise_lfsr;
    mus_voice_t    voices[MUS_TOTAL_VOICE];
    uint8_t        ch_vol[MUS_MAX_CHANNELS];
    uint8_t        ch_vel[MUS_MAX_CHANNELS];
    uint8_t        playing;
    uint8_t        looping;
    uint8_t        eos;
    uint8_t        dbg_event_budget;
    uint32_t       dbg_batches;
    uint32_t       dbg_events;
    uint32_t       dbg_note_on;
    uint32_t       dbg_note_off;
    uint32_t       dbg_read_calls;
    uint32_t       dbg_generated_samples;
    uint32_t       dbg_nonzero_samples;
    int32_t        dbg_peak_abs;
    uint8_t        dbg_reported_nonzero;
    uint8_t        dbg_reported_silence;
    uint8_t        _pad[2];
} n64_mus_player_t;

static n64_mus_player_t mus_player;

static boolean N64_IsMus(const void *data, int len)
{
    const uint8_t *d = (const uint8_t *)data;
    return data != NULL && len >= 16
        && d[0] == 'M' && d[1] == 'U' && d[2] == 'S' && d[3] == '\x1a';
}

static boolean N64_MusReadByte(uint8_t *out)
{
    if (mus_player.score_pos >= mus_player.score_len)
    {
        mus_player.eos = 1;
        return false;
    }

    *out = mus_player.score[mus_player.score_pos++];
    return true;
}

static void N64_MusProcessBatch(void)
{
    uint8_t ev, b, ctrl, val, note_byte, note;
    int last, ch, type, slot;
    uint16_t delay;
    float freq;

    mus_player.dbg_batches++;

    for (;;)
    {
        if (!N64_MusReadByte(&ev))
            return;

        last = (ev >> 7) & 1;
        ch   = ev & 0x0F;
        type = (ev >> 4) & 0x07;
        slot = ch;
        mus_player.dbg_events++;

        if (mus_player.dbg_event_budget > 0)
        {
            debugf("MUS event[%u]: type=%d ch=%d last=%d pos=%d\n",
                   (unsigned)mus_player.dbg_events,
                   type,
                   ch,
                   last,
                   mus_player.score_pos - 1);
            mus_player.dbg_event_budget--;
        }

        switch (type)
        {
        case 0: /* release note: 1 byte (note) */
            if (!N64_MusReadByte(&note))
                return;
            mus_player.voices[slot].active = 0;
            mus_player.dbg_note_off++;
            break;

        case 1: /* play note: 1-2 bytes */
            if (!N64_MusReadByte(&note_byte))
                return;
            note = note_byte & 0x7F;
            if (note_byte & 0x80)
            {
                if (!N64_MusReadByte(&mus_player.ch_vel[ch]))
                    return;
            }

            freq = 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
            mus_player.voices[slot].phase_inc =
                (uint32_t)(freq * (4294967296.0f / (float)N64_AUDIO_FREQUENCY));
            mus_player.voices[slot].note     = note;
            mus_player.voices[slot].ch_vol   = mus_player.ch_vol[ch];
            mus_player.voices[slot].velocity = mus_player.ch_vel[ch];
            mus_player.voices[slot].phase    = 0;
            mus_player.voices[slot].active   = 1;
            mus_player.voices[slot].is_perc  = (uint8_t)(ch == MUS_PERC_CHANNEL);
            if (ch == MUS_PERC_CHANNEL)
                mus_player.voices[slot].perc_left = MUS_PERC_DECAY;
            mus_player.dbg_note_on++;

            if (mus_player.dbg_event_budget > 0)
            {
                debugf("MUS note on: ch=%d note=%d vel=%d chvol=%d\n",
                       ch,
                       note,
                       mus_player.ch_vel[ch],
                       mus_player.ch_vol[ch]);
                mus_player.dbg_event_budget--;
            }
            break;

        case 2: /* pitch wheel: 1 byte */
            if (!N64_MusReadByte(&b))
                return;
            if (mus_player.voices[slot].active)
            {
                float bend = ((float)(int)b - 128.0f) / 64.0f;
                float base = 440.0f * powf(2.0f,
                    ((float)mus_player.voices[slot].note - 69.0f) / 12.0f);
                freq = base * powf(2.0f, bend / 12.0f);
                mus_player.voices[slot].phase_inc =
                    (uint32_t)(freq * (4294967296.0f / (float)N64_AUDIO_FREQUENCY));
            }
            break;

        case 3: /* system event: 1 byte (ignored) */
            if (!N64_MusReadByte(&b))
                return;
            break;

        case 4: /* change controller: 2 bytes */
            if (!N64_MusReadByte(&ctrl) || !N64_MusReadByte(&val))
                return;
            if (ctrl == 3) /* volume */
            {
                mus_player.ch_vol[ch] = val;
                if (mus_player.voices[slot].active)
                    mus_player.voices[slot].ch_vol = val;

                if (mus_player.dbg_event_budget > 0)
                {
                    debugf("MUS volume: ch=%d val=%d\n", ch, val);
                    mus_player.dbg_event_budget--;
                }
            }
            break;

        case 6: /* score end */
            debugf("MUS score end: events=%u notes_on=%u notes_off=%u pos=%d/%d\n",
                   (unsigned)mus_player.dbg_events,
                   (unsigned)mus_player.dbg_note_on,
                   (unsigned)mus_player.dbg_note_off,
                   mus_player.score_pos,
                   mus_player.score_len);
            mus_player.eos = 1;
            return;

        default: /* case 5 (end of measure) and 7 (unused): no extra bytes */
            if (type == 7)
                debugf("MUS warning: reserved event type 7 at pos=%d\n", mus_player.score_pos - 1);
            break;
        }

        if (!last)
            continue;

        /* last event in batch: read variable-length delay */
        delay = 0;
        do
        {
            if (!N64_MusReadByte(&b))
                return;
            delay = (uint16_t)(delay * 128 + (b & 0x7F));
        } while (b & 0x80);

        mus_player.tick_delay = (int32_t)delay;

        if (mus_player.dbg_event_budget > 0)
        {
            debugf("MUS delay: %u ticks\n", (unsigned)delay);
            mus_player.dbg_event_budget--;
        }

        return;
    }
}

static void N64_MusWaveRead(void *ctx, samplebuffer_t *sbuf,
                            int wpos, int wlen, bool seeking)
{
    int16_t *buf;
    int i, v;
    int32_t sum;
    int32_t abs_sum;
    uint32_t half;
    int16_t wsmp;
    int32_t vscale;

    (void)ctx;

    mus_player.dbg_read_calls++;

    if (mus_player.dbg_read_calls == 1)
    {
        debugf("MUS read begin: wpos=%d wlen=%d seeking=%d\n",
               wpos,
               wlen,
               seeking ? 1 : 0);
    }

    if (seeking)
    {
        mus_player.score_pos  = 0;
        mus_player.tick_delay = 0;
        mus_player.tick_frac  = 0;
        for (v = 0; v < MUS_TOTAL_VOICE; v++)
            mus_player.voices[v].active = 0;
        mus_player.eos = 0;
    }

    buf = (int16_t *)samplebuffer_append(sbuf, wlen);
    if (!buf)
        return;

    for (i = 0; i < wlen; i++)
    {
        /* advance MUS tick counter */
        mus_player.tick_frac += MUS_TICK_RATE;
        while (mus_player.tick_frac >= N64_AUDIO_FREQUENCY)
        {
            mus_player.tick_frac -= N64_AUDIO_FREQUENCY;
            if (!mus_player.eos)
            {
                if (mus_player.tick_delay > 0)
                    mus_player.tick_delay--;
                if (mus_player.tick_delay == 0)
                {
                    do { N64_MusProcessBatch(); }
                    while (mus_player.tick_delay == 0 && !mus_player.eos);
                }
                if (mus_player.eos)
                {
                    if (mus_player.looping)
                    {
                        mus_player.score_pos  = 0;
                        mus_player.tick_delay = 0;
                        mus_player.eos        = 0;
                        for (v = 0; v < MUS_TOTAL_VOICE; v++)
                            mus_player.voices[v].active = 0;
                        N64_MusProcessBatch();
                    }
                    else
                    {
                        mus_player.playing = 0;
                    }
                }
            }
        }

        /* synthesize sample: sum all active voices */
        sum = 0;
        for (v = 0; v < MUS_TOTAL_VOICE; v++)
        {
            if (!mus_player.voices[v].active)
                continue;

            mus_player.voices[v].phase += mus_player.voices[v].phase_inc;

            if (mus_player.voices[v].is_perc)
            {
                if (mus_player.voices[v].perc_left <= 0)
                {
                    mus_player.voices[v].active = 0;
                    continue;
                }
                /* xorshift noise + linear decay envelope */
                mus_player.noise_lfsr ^= mus_player.noise_lfsr >> 13;
                mus_player.noise_lfsr ^= mus_player.noise_lfsr << 17;
                mus_player.noise_lfsr ^= mus_player.noise_lfsr >> 5;
                wsmp = (int16_t)(mus_player.noise_lfsr >> 16);
                wsmp = (int16_t)((int32_t)wsmp
                    * mus_player.voices[v].perc_left / MUS_PERC_DECAY);
                mus_player.voices[v].perc_left--;
            }
            else
            {
                /* triangle wave */
                half = (mus_player.voices[v].phase < 0x80000000u)
                     ? (mus_player.voices[v].phase * 2u)
                     : ((0xFFFFFFFFu - mus_player.voices[v].phase) * 2u);
                wsmp = (int16_t)((int32_t)(half >> 16) - 32768);
            }

            vscale = ((int32_t)mus_player.voices[v].ch_vol
                    * (int32_t)mus_player.voices[v].velocity) >> 7;
            sum += ((int32_t)wsmp * vscale) >> 7;
        }

        /* scale down and clamp */
        sum >>= 3;
        if (sum >  32767) sum =  32767;
        else if (sum < -32768) sum = -32768;
        buf[i] = (int16_t)sum;

        abs_sum = (sum < 0) ? -sum : sum;
        if (abs_sum > mus_player.dbg_peak_abs)
            mus_player.dbg_peak_abs = abs_sum;

        if (sum != 0)
        {
            mus_player.dbg_nonzero_samples++;
            if (!mus_player.dbg_reported_nonzero)
            {
                debugf("MUS first nonzero sample at %u (amp=%d, notes_on=%u, events=%u)\n",
                       (unsigned)mus_player.dbg_generated_samples,
                       (int)sum,
                       (unsigned)mus_player.dbg_note_on,
                       (unsigned)mus_player.dbg_events);
                mus_player.dbg_reported_nonzero = 1;
            }
        }

        mus_player.dbg_generated_samples++;
    }

    if (!mus_player.dbg_reported_silence
        && mus_player.dbg_generated_samples >= MUS_DEBUG_SILENCE_SAMPLES
        && mus_player.dbg_nonzero_samples == 0)
    {
        debugf("MUS silence warning: generated=%u events=%u notes_on=%u score_pos=%d/%d\n",
               (unsigned)mus_player.dbg_generated_samples,
               (unsigned)mus_player.dbg_events,
               (unsigned)mus_player.dbg_note_on,
               mus_player.score_pos,
               mus_player.score_len);
        mus_player.dbg_reported_silence = 1;
    }
}

static boolean N64_MusSetup(const void *data, int len)
{
    const uint8_t *d = (const uint8_t *)data;
    int score_start, score_len;
    int prim_channels, sec_channels, inst_count;

    if (!N64_IsMus(data, len))
        return false;

    score_len   = (int)N64_ReadLE16(d + 4);
    score_start = (int)N64_ReadLE16(d + 6);
    prim_channels = (int)N64_ReadLE16(d + 8);
    sec_channels  = (int)N64_ReadLE16(d + 10);
    inst_count    = (int)N64_ReadLE16(d + 12);

    if (score_start < 16 || score_start > len
        || score_len <= 0 || score_start + score_len > len)
    {
        debugf("I_RegisterSong: malformed MUS header (start=%d len=%d total=%d)\n",
               score_start, score_len, len);
        return false;
    }

    memset(&mus_player, 0, sizeof(mus_player));
    mus_player.score      = d + score_start;
    mus_player.score_len  = score_len;
    mus_player.score_pos  = 0;
    mus_player.noise_lfsr = 0xDEADBEEFu;
        mus_player.dbg_event_budget = MUS_DEBUG_EVENT_LOG_BUDGET;
    memset(mus_player.ch_vol, 127, sizeof(mus_player.ch_vol));
    memset(mus_player.ch_vel, 100, sizeof(mus_player.ch_vel));

        debugf("MUS setup: score_start=%d score_len=%d channels=%d secondary=%d instruments=%d\n",
            score_start,
            score_len,
            prim_channels,
            sec_channels,
            inst_count);

    mus_player.wave.name      = "MUS";
    mus_player.wave.bits      = 16;
    mus_player.wave.channels  = 1;
    mus_player.wave.frequency = (float)N64_AUDIO_FREQUENCY;
    mus_player.wave.len       = WAVEFORM_UNKNOWN_LEN;
    mus_player.wave.loop_len  = 0;
    mus_player.wave.read      = N64_MusWaveRead;
    mus_player.wave.ctx       = &mus_player;

    debugf("MUS setup: waveform ready (rate=%dHz)\n", N64_AUDIO_FREQUENCY);

    return true;
}

static void N64_ApplyMusicVolume(void)
{
    int volume;
    float gain;

    if (!music_track.opened)
        return;

    volume = N64_NormalizeVolume(snd_MusicVolume);

    gain = (float)volume / 127.0f;

    if (music_track.format == N64_MUSIC_FMT_XM64)
    {
        xm64player_set_vol(&music_track.xm, gain);
    }
    else if (music_track.format == N64_MUSIC_FMT_YM64)
    {
        int i;

        for (i = 0; i < music_track.channels; i++)
        {
            int ch = music_first_channel + i;
            if (ch >= 0 && ch < mixer_channel_count)
                mixer_ch_set_vol(ch, gain, gain);
        }
    }
    else if (music_track.format == N64_MUSIC_FMT_MUS)
    {
        mixer_ch_set_vol(music_first_channel, gain, gain);
    }
}

static boolean N64_OpenMusicTrack(void)
{
    if (!music_track.registered)
        return false;
    if (!music_track.asset_path[0] && music_track.format != N64_MUSIC_FMT_MUS)
        return false;

    if (music_track.opened)
        return true;

    if (music_channel_count <= 0)
    {
        debugf("I_PlaySong: no mixer channels available for music\n");
        return false;
    }

    if (music_track.format == N64_MUSIC_FMT_XM64)
    {
        xm64player_open(&music_track.xm, music_track.asset_path);
        music_track.channels = xm64player_num_channels(&music_track.xm);
        if (music_track.channels < 1)
            music_track.channels = 1;

        if (music_track.channels > music_channel_count)
        {
            debugf("I_PlaySong: %s needs %d channels, only %d reserved\n",
                   music_track.asset_path,
                   music_track.channels,
                   music_channel_count);
            xm64player_close(&music_track.xm);
            return false;
        }
    }
    else if (music_track.format == N64_MUSIC_FMT_YM64)
    {
        ym64player_open(&music_track.ym, music_track.asset_path, NULL);
        music_track.channels = ym64player_num_channels(&music_track.ym);
        if (music_track.channels < 1)
            music_track.channels = 1;

        if (music_track.channels > music_channel_count)
        {
            debugf("I_PlaySong: %s needs %d channels, only %d reserved\n",
                   music_track.asset_path,
                   music_track.channels,
                   music_channel_count);
            ym64player_close(&music_track.ym);
            return false;
        }
    }
    else if (music_track.format == N64_MUSIC_FMT_MUS)
    {
        if (!N64_MusSetup(music_track.mus_data, music_track.mus_data_len))
            return false;
        music_track.channels = 1;
    }
    else
    {
        return false;
    }

    music_track.opened = true;
    N64_ApplyMusicVolume();
    return true;
}

static void N64_StartMusicTrack(boolean looping)
{
    if (!N64_OpenMusicTrack())
        return;

    if (music_track.format == N64_MUSIC_FMT_XM64)
    {
        xm64player_set_loop(&music_track.xm, looping ? true : false);
        xm64player_play(&music_track.xm, music_first_channel);
    }
    else if (music_track.format == N64_MUSIC_FMT_YM64)
    {
        ym64player_play(&music_track.ym, music_first_channel);
    }
    else if (music_track.format == N64_MUSIC_FMT_MUS)
    {
        mus_player.looping = looping ? 1 : 0;
        mus_player.playing = 1;
        mus_player.eos     = 0;
        debugf("I_PlaySong: MUS start lump=%s loop=%d channel=%d\n",
               music_track.lump_name,
               looping ? 1 : 0,
               music_first_channel);
        mixer_ch_play(music_first_channel, &mus_player.wave);
    }

    music_track.looping = looping;
    music_track.playing = true;
    music_track.paused = false;
    N64_ApplyMusicVolume();
}

static void N64_StopMusicTrack(void)
{
    if (!music_track.opened)
        return;

    if (music_track.format == N64_MUSIC_FMT_XM64)
        xm64player_stop(&music_track.xm);
    else if (music_track.format == N64_MUSIC_FMT_YM64)
        ym64player_stop(&music_track.ym);
    else if (music_track.format == N64_MUSIC_FMT_MUS)
    {
        mixer_ch_stop(music_first_channel);
        mus_player.playing = 0;
        debugf("I_StopSong: MUS stop samples=%u nonzero=%u peak=%d events=%u notes_on=%u\n",
               (unsigned)mus_player.dbg_generated_samples,
               (unsigned)mus_player.dbg_nonzero_samples,
               (int)mus_player.dbg_peak_abs,
               (unsigned)mus_player.dbg_events,
               (unsigned)mus_player.dbg_note_on);
    }

    music_track.playing = false;
    music_track.paused = false;
}

static void N64_MaybeLoopMusic(void)
{
    if (!music_track.opened || !music_track.playing)
        return;

    if (music_track.format == N64_MUSIC_FMT_MUS
        && !mus_player.playing)
    {
        debugf("MUS playback finished: eos=%d pos=%d/%d events=%u notes_on=%u nonzero=%u\n",
               mus_player.eos ? 1 : 0,
               mus_player.score_pos,
               mus_player.score_len,
               (unsigned)mus_player.dbg_events,
               (unsigned)mus_player.dbg_note_on,
               (unsigned)mus_player.dbg_nonzero_samples);
        mixer_ch_stop(music_first_channel);
        music_track.playing = false;
        return;
    }

    if (music_track.format == N64_MUSIC_FMT_YM64
        && !mixer_ch_playing(music_first_channel))
    {
        if (!music_track.looping)
        {
            music_track.playing = false;
            return;
        }

        if (!ym64player_seek(&music_track.ym, 0))
        {
            ym64player_close(&music_track.ym);
            ym64player_open(&music_track.ym, music_track.asset_path, NULL);
        }

        ym64player_play(&music_track.ym, music_first_channel);
        N64_ApplyMusicVolume();
    }
}

static void N64_PumpAudio(void)
{
    int samples;

    if (!sound_initialized)
        return;

    N64_MaybeLoopMusic();

    samples = audio_get_buffer_length();

    while (audio_can_write())
    {
        int16_t* out = audio_write_begin();
        mixer_poll(out, samples);
        audio_write_end();
    }
}

void I_InitSound(void)
{
    int i;

    if (sound_initialized)
        return;

    if (numChannels < 1)
        numChannels = 1;
    if (numChannels > MIXER_MAX_CHANNELS - 1)
        numChannels = MIXER_MAX_CHANNELS - 1;

    voice_count = numChannels;
    mixer_channel_count = voice_count + N64_MUSIC_CHANNEL_BUDGET;
    if (mixer_channel_count > MIXER_MAX_CHANNELS)
        mixer_channel_count = MIXER_MAX_CHANNELS;

    music_first_channel = voice_count;
    music_channel_count = mixer_channel_count - music_first_channel;

    audio_init(N64_AUDIO_FREQUENCY, N64_AUDIO_NUM_BUFFERS);
    mixer_init(mixer_channel_count);

    for (i = 0; i < voice_count; i++)
    {
        mixer_ch_set_limits(i, 8, (float)(N64_DEFAULT_SFX_RATE * 2), 0);
        N64_ClearVoice(i);
    }

    for (i = music_first_channel; i < mixer_channel_count; i++)
        mixer_ch_set_limits(i, 16, 48000.0f, 0);

    if (!music_initialized)
        I_InitMusic();

    N64_LoadAllSfx();

    sound_initialized = true;
    N64_PumpAudio();

    debugf("I_InitSound: %d SFX channels, %d music channels, %d Hz output\n",
           voice_count,
           music_channel_count,
           audio_get_frequency());
}

void I_UpdateSound(void)
{
}

void I_SubmitSound(void)
{
    N64_PumpAudio();
}

void I_ShutdownSound(void)
{
    int i;

    if (!sound_initialized)
        return;

    N64_ResetMusicTrack(true);

    for (i = 0; i < voice_count; i++)
    {
        mixer_ch_stop(i);
        N64_ClearVoice(i);
    }

    mixer_close();
    audio_close();

    for (i = 1; i < NUMSFX; i++)
    {
        if (sfx_cache[i].owns_pcm && sfx_cache[i].pcm)
            free(sfx_cache[i].pcm);
        S_sfx[i].data = NULL;
    }

    memset(sfx_cache, 0, sizeof(sfx_cache));
    voice_count = 0;
    mixer_channel_count = 0;
    music_first_channel = 0;
    music_channel_count = 0;
    sound_initialized = false;
}

void I_SetChannels(void)
{
    int i;

    for (i = 0; i < voice_count; i++)
        N64_ClearVoice(i);
}

int I_GetSfxLumpNum(sfxinfo_t* sfxinfo)
{
    return N64_GetSfxLumpByName(sfxinfo->name);
}

int
I_StartSound
( int id,
  int vol,
  int sep,
  int pitch,
  int priority )
{
    int voice;
    int handle;

    (void)priority;

    if (!sound_initialized)
        return 0;

    if (id <= 0 || id >= NUMSFX)
        return 0;

    if (!sfx_cache[id].loaded || !sfx_cache[id].pcm || sfx_cache[id].length <= 0)
        return 0;

    voice = N64_AllocVoice();
    if (voices[voice].active)
        mixer_ch_stop(voice);

    if (++next_handle <= 0)
        next_handle = 1;

    handle = next_handle;
    voices[voice].active = true;
    voices[voice].handle = handle;
    voices[voice].sfx_id = id;
    voices[voice].start_tic = gametic;

    mixer_ch_play(voice, &sfx_cache[id].wave);
    N64_ApplyChannelParams(voice, vol, sep, pitch);

    return handle;
}

void I_StopSound(int handle)
{
    int voice;

    voice = N64_FindVoiceByHandle(handle);
    if (voice < 0)
        return;

    mixer_ch_stop(voice);
    N64_ClearVoice(voice);
}

int I_SoundIsPlaying(int handle)
{
    int voice;

    voice = N64_FindVoiceByHandle(handle);
    if (voice < 0)
        return 0;

    if (!mixer_ch_playing(voice))
    {
        N64_ClearVoice(voice);
        return 0;
    }

    return 1;
}

void
I_UpdateSoundParams
( int handle,
  int vol,
  int sep,
  int pitch )
{
    int voice;

    voice = N64_FindVoiceByHandle(handle);
    if (voice < 0)
        return;

    if (!mixer_ch_playing(voice))
    {
        N64_ClearVoice(voice);
        return;
    }

    N64_ApplyChannelParams(voice, vol, sep, pitch);
}

void I_InitMusic(void)
{
    if (music_initialized)
        return;

    N64_ResetMusicTrack(false);
    xm64_set_extsampledir("rom:/music/samples");
    missing_music_warning_printed = false;
    music_initialized = true;
}

void I_ShutdownMusic(void)
{
    if (!music_initialized)
        return;

    N64_ResetMusicTrack(true);
    music_initialized = false;
}

void I_SetMusicVolume(int volume)
{
    if (volume < 0)
        volume = 0;
    else if (volume > 127)
        volume = 127;

    snd_MusicVolume = volume;
    N64_ApplyMusicVolume();
}

void I_PauseSong(int handle)
{
    if (handle != music_track.handle)
        return;

    if (!music_track.opened || !music_track.playing)
        return;

    N64_StopMusicTrack();
    music_track.paused = true;
}

void I_ResumeSong(int handle)
{
    if (handle != music_track.handle)
        return;

    if (!sound_initialized || !music_track.opened || !music_track.paused)
        return;

    if (music_track.format == N64_MUSIC_FMT_XM64)
        xm64player_play(&music_track.xm, music_first_channel);
    else if (music_track.format == N64_MUSIC_FMT_YM64)
        ym64player_play(&music_track.ym, music_first_channel);
    else if (music_track.format == N64_MUSIC_FMT_MUS)
    {
        mus_player.playing = 1;
        mixer_ch_play(music_first_channel, &mus_player.wave);
    }

    music_track.playing = true;
    music_track.paused = false;
    N64_ApplyMusicVolume();
}

int I_RegisterSong(void* data)
{
    int llen;

    if (!music_initialized)
        I_InitMusic();

    N64_ResetMusicTrack(true);

    if (++next_music_handle <= 0)
        next_music_handle = 1;

    music_track.handle = next_music_handle;
    music_track.registered = true;
    music_track.lumpnum = N64_FindLumpNumByData(data);
        llen = (music_track.lumpnum >= 0)
            ? W_LumpLength(music_track.lumpnum) : 0;

        debugf("I_RegisterSong: handle=%d lumpnum=%d len=%d data=%p\n",
            music_track.handle,
            music_track.lumpnum,
            llen,
            data);

    if (N64_ResolveMusicTrackAsset(music_track.lumpnum))
    {
        debugf("I_RegisterSong: %s -> %s\n",
               music_track.lump_name,
               music_track.asset_path);
    }

    if (music_track.format == N64_MUSIC_FMT_NONE && data)
    {
        if (N64_IsMus(data, llen))
        {
            music_track.mus_data     = data;
            music_track.mus_data_len = llen;
            music_track.format       = N64_MUSIC_FMT_MUS;
            debugf("I_RegisterSong: MUS soft-synth -> %s\n",
                   music_track.lump_name[0] ? music_track.lump_name : "?");
        }
        else if (!missing_music_warning_printed)
        {
            debugf("I_RegisterSong: no .xm64/.ym64 and not MUS format\n");
            missing_music_warning_printed = true;
        }
    }

    return next_music_handle;
}

void
I_PlaySong
( int handle,
  int looping )
{
    debugf("I_PlaySong: request handle=%d active=%d format=%d looping=%d opened=%d\n",
           handle,
           music_track.handle,
           music_track.format,
           looping,
           music_track.opened ? 1 : 0);

    if (handle != music_track.handle)
        return;

    if (!sound_initialized || !music_track.registered)
        return;

    if (!music_track.asset_path[0] && music_track.format != N64_MUSIC_FMT_MUS)
        return;

    if (music_track.playing || music_track.paused)
        N64_StopMusicTrack();

    N64_StartMusicTrack(looping ? true : false);

    if (!music_track.playing)
    {
        debugf("I_PlaySong: failed to start music asset %s\n",
               music_track.asset_path);
    }
}

void I_StopSong(int handle)
{
    if (handle != music_track.handle)
        return;

    N64_StopMusicTrack();
}

void I_UnRegisterSong(int handle)
{
    if (handle != music_track.handle)
        return;

    N64_ResetMusicTrack(true);
}
