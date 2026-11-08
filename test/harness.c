/*
 * neo-pi - Linux test harness: the same core and set-up as the kernel, frames
 * as PPM and the sound as WAV
 *
 * Build with the core's sources as build.sh copies them, after
 * patches/geolith-phase.py (in a directory of your choice, here geo/):
 *   gcc -O2 -I third_party/geolith/src -I third_party/geolith/src/m68k \
 *       -I third_party/geolith/src/z80 -I third_party/geolith/src/ymfm \
 *       -I third_party/geolith/deps -I third_party/geolith/deps/miniz \
 *       -I third_party/geolith/deps/speex -o harness test/harness.c geo/[a-z]*.c \
 *       src/geo_disc_stub.c -lm
 *   ./harness neogeo.zip game.neo frames every outdir
 *
 * Environment:
 *   RAW=1        the raw YM2610 output (no Speex), as neo-pi's resampler gets it
 *   KEYS=f:c,... player 1 holds c for 6 frames from frame f: u d l r (joystick),
 *                a b c D (buttons), s (START, 6 frames), k (coin, 8 frames)
 *   TEST_AT=f    MVS TEST button from frame f, for TEST_LEN frames (default 10)
 *   DIP1=f       DIP switch 1 on until frame f
 *   FILL=1       the picture buffer filled with magenta before the start
 *   SECOND=n     play n frames, then load the game again as the kernel does
 *                (UnloadGame + LoadGame); CLEAR=1 clears the video memory too
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "geo.h"
#include "geo_lspc.h"
#include "geo_mixer.h"
#include "geo_neo.h"

static uint32_t vbuf[320 * 264];
static int16_t abuf[8192];

static FILE *wav;
static size_t wavsamples;		/* int16 values written */
static size_t lastsamps;		/* int16 values of the last frame */

static int g_frame;
static int test_at = -1, test_len = 10, dip1_until = -1;
static const char *keys = 0;

static void audio_cb (size_t n)
{
	fwrite (abuf, 2, n, wav);
	wavsamples += n;
	lastsamps = n;
}

static void log_cb (int level, const char *fmt, ...)
{
	if (level == GEO_LOG_DBG)
		return;
	va_list a;
	va_start (a, fmt);
	vfprintf (stderr, fmt, a);
	va_end (a);
}

/* is code held at the current frame? (KEYS) */
static int held (char code, int frames)
{
	for (const char *k = keys; k && *k; )
	{
		int f = atoi (k);
		const char *c = strchr (k, ':');
		if (!c)
			break;
		if (c[1] == code && g_frame >= f && g_frame < f + frames)
			return 1;
		const char *n = strchr (k, ',');
		if (!n)
			break;
		k = n + 1;
	}
	return 0;
}

/* inputs, all active low */
static unsigned js (unsigned port)
{
	if (port != 0 || !keys)
		return 0xff;
	static const char codes[8] = {'u', 'd', 'l', 'r', 'a', 'b', 'c', 'D'};
	unsigned v = 0xff;
	for (unsigned b = 0; b < 8; b++)
		if (held (codes[b], 6))
			v &= ~(1u << b);
	return v;
}

static unsigned sa (void)	{ return held ('k', 8) ? (0x07 | 0x18) & ~1u : 0x07 | 0x18; }
static unsigned sb (void)	{ return held ('s', 6) ? (0x0f | 0x30) & ~1u : 0x0f | 0x30; }
static unsigned st (void)	{ return test_at >= 0 && g_frame >= test_at && g_frame < test_at + test_len ? 0x40 : 0xc0; }
static unsigned dip (void)	{ return g_frame < dip1_until ? 0xfe : 0xff; }
static unsigned un (void)	{ return 0xff; }

static void *read_file (const char *path, size_t *size)
{
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		perror (path);
		exit (1);
	}
	fseek (f, 0, SEEK_END);
	*size = ftell (f);
	rewind (f);
	void *b = malloc (*size);
	if (fread (b, 1, *size, f) != *size)
		exit (1);
	fclose (f);
	return b;
}

static void put32 (FILE *f, uint32_t v)	{ fwrite (&v, 4, 1, f); }
static void put16 (FILE *f, uint16_t v)	{ fwrite (&v, 2, 1, f); }

/* 48 kHz, 16-bit stereo */
static void wav_header (FILE *f, size_t samples)
{
	uint32_t data = samples * 2;
	fseek (f, 0, SEEK_SET);
	fwrite ("RIFF", 1, 4, f);
	put32 (f, 36 + data);
	fwrite ("WAVEfmt ", 1, 8, f);
	put32 (f, 16);
	put16 (f, 1);
	put16 (f, 2);
	put32 (f, 48000);
	put32 (f, 48000 * 4);
	put16 (f, 4);
	put16 (f, 16);
	fwrite ("data", 1, 4, f);
	put32 (f, data);
}

/* the visible 304x224 area */
static void write_ppm (const char *path)
{
	FILE *f = fopen (path, "wb");
	fprintf (f, "P6 304 224 255\n");
	for (int y = 0; y < 224; y++)
	{
		for (int x = 0; x < 304; x++)
		{
			uint32_t c = vbuf[(y + 24) * 320 + x + 8];
			unsigned char rgb[3] = {c >> 16, c >> 8, c};
			fwrite (rgb, 1, 3, f);
		}
	}
	fclose (f);
}

/* as CKernel::ClearVideoMemory */
static void clear_video_memory (void)
{
	for (unsigned bank = 0; bank < 2; bank++)
	{
		geo_lspc_palram_bank (bank);
		for (uint32_t a = 0; a < 0x2000; a += 2)
			geo_lspc_palram_wr16 (a, 0);
	}
	geo_lspc_palram_bank (0);
	geo_lspc_vrammod_wr (1);
	geo_lspc_vramaddr_wr (0);
	for (int i = 0; i < 0x8000; i++)
		geo_lspc_vram_wr (0);
	geo_lspc_vramaddr_wr (0x8000);
	for (int i = 0; i < 0x800; i++)
		geo_lspc_vram_wr (0);
	geo_lspc_vramaddr_wr (0);
	geo_lspc_vrammod_wr (0);
}

static int cmp_double (const void *a, const void *b)
{
	double x = *(const double *) a, y = *(const double *) b;
	return x < y ? -1 : x > y;
}

static double ms_between (const struct timespec *a, const struct timespec *b)
{
	return (b->tv_sec - a->tv_sec) * 1e3 + (b->tv_nsec - a->tv_nsec) / 1e6;
}

int main (int argc, char **argv)
{
	if (argc < 6)
	{
		fprintf (stderr, "usage: bios.zip game.neo frames every outdir\n");
		return 1;
	}
	size_t bn, gn;
	void *bios = read_file (argv[1], &bn);
	void *game = read_file (argv[2], &gn);
	int frames = atoi (argv[3]), every = atoi (argv[4]);
	const char *out = argv[5];
	char path[512];
	snprintf (path, sizeof path, "%s/audio.wav", out);
	wav = fopen (path, "wb");
	wav_header (wav, 0);

	geo_log_set_callback (log_cb);
	geo_set_region (REGION_EU);
	geo_set_system (SYSTEM_MVS);
	geo_mixer_set_rate (48000);
	geo_mixer_init ();
	if (getenv ("RAW"))
		geo_mixer_set_raw (1);
	geo_lspc_set_buffer (vbuf);
	geo_mixer_set_buffer (abuf);
	geo_mixer_set_callback (audio_cb);
	geo_init ();
	geo_set_region (REGION_EU);
	if (!geo_bios_load_mem (bios, bn))
	{
		fprintf (stderr, "BIOS KO\n");
		return 2;
	}
	if (!geo_neo_load (game, gn))
	{
		fprintf (stderr, "NEO KO\n");
		return 3;
	}
	geo_input_set_callback (0, js);
	geo_input_set_callback (1, js);
	geo_input_sys_set_callback (0, sa);
	geo_input_sys_set_callback (1, sb);
	geo_input_sys_set_callback (2, st);
	geo_input_sys_set_callback (3, dip);
	geo_input_sys_set_callback (4, un);
	if (getenv ("TEST_AT"))
		test_at = atoi (getenv ("TEST_AT"));
	if (getenv ("TEST_LEN"))
		test_len = atoi (getenv ("TEST_LEN"));
	if (getenv ("DIP1"))
		dip1_until = atoi (getenv ("DIP1"));
	keys = getenv ("KEYS");
	geo_lspc_set_palette (0);
	geo_reset (1);
	if (getenv ("FILL"))
		for (int i = 0; i < 320 * 264; i++)
			vbuf[i] = 0x00FF00FF;

	if (getenv ("SECOND"))
	{
		/* play a while, then load the game again like the kernel */
		int n = atoi (getenv ("SECOND"));
		for (int i = 0; i < n; i++)
			geo_exec ();
		geo_deinit ();
		geo_bios_unload ();
		geo_mixer_deinit ();
		geo_set_system (SYSTEM_MVS);
		geo_set_region (REGION_EU);
		geo_mixer_set_rate (48000);
		geo_mixer_init ();
		geo_init ();
		geo_set_region (REGION_EU);
		if (!geo_bios_load_mem (bios, bn))
			return 2;
		if (!geo_neo_load (game, gn))
			return 3;
		if (getenv ("CLEAR"))
			clear_video_memory ();
		geo_lspc_set_palette (0);
		geo_reset (1);
	}

	struct timespec t0, t1;
	clock_gettime (CLOCK_MONOTONIC, &t0);
	size_t min_samps = 1 << 30, max_samps = 0;
	static double frame_ms[100000];
	int nframe_ms = 0;
	for (int i = 1; i <= frames; i++)
	{
		struct timespec a, b;
		clock_gettime (CLOCK_MONOTONIC, &a);
		g_frame = i;
		geo_exec ();
		clock_gettime (CLOCK_MONOTONIC, &b);
		if (i > 600)		/* after the BIOS start-up */
			frame_ms[nframe_ms++] = ms_between (&a, &b);
		if (lastsamps < min_samps)
			min_samps = lastsamps;
		if (lastsamps > max_samps)
			max_samps = lastsamps;
		if (i % every == 0)
		{
			snprintf (path, sizeof path, "%s/f%05d.ppm", out, i);
			write_ppm (path);
		}
	}
	clock_gettime (CLOCK_MONOTONIC, &t1);
	wav_header (wav, wavsamples);
	fclose (wav);

	double s = ms_between (&t0, &t1) / 1000;
	printf ("%d frame in %.2f s (%.3f ms/frame, host x86) audio %zu..%zu int16/frame\n",
		frames, s, s * 1000 / frames, min_samps, max_samps);
	qsort (frame_ms, nframe_ms, sizeof (double), cmp_double);
	double sum = 0;
	for (int i = 0; i < nframe_ms; i++)
		sum += frame_ms[i];
	printf ("after boot: avg %.3f p99 %.3f max %.3f ms\n", sum / nframe_ms,
		frame_ms[nframe_ms * 99 / 100], frame_ms[nframe_ms - 1]);
	return 0;
}
