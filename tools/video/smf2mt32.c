// SPDX-License-Identifier: GPL-2.0-or-later
/* Origin: written in the u7port session (the user's Ultima 7 port) for its MT-32 video; reused here unchanged. */
/* smf2mt32 <in.mid> <out.wav> <romdir>: renders a Standard MIDI File (all tracks merged, tempo
 * map honoured, SysEx included) through libmt32emu (MT-32 1.07 ROMs) to 32 kHz stereo WAV. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mt32emu/c_interface/c_interface.h>

typedef struct { double sec; uint8_t *data; uint32_t len; int order; } Event;
static Event *ev; static size_t nev, cap;
static void add(double sec, const uint8_t *d, uint32_t len, int order)
{
	if (nev == cap) ev = realloc(ev, (cap = cap ? cap * 2 : 1024) * sizeof *ev);
	ev[nev].sec = sec; ev[nev].data = malloc(len); memcpy(ev[nev].data, d, len);
	ev[nev].len = len; ev[nev].order = order; nev++;
}
static int cmp(const void *a, const void *b)
{
	const Event *x = a, *y = b;
	if (x->sec != y->sec) return x->sec < y->sec ? -1 : 1;
	return x->order - y->order;
}
static uint32_t be(const uint8_t *p, int n) { uint32_t v = 0; while (n--) v = v << 8 | *p++; return v; }
static uint32_t vlq(const uint8_t **p) { uint32_t v = 0; uint8_t c; do { c = *(*p)++; v = v << 7 | (c & 0x7f); } while (c & 0x80); return v; }

/* tempo map: (tick, usec per quarter) */
static struct { uint32_t tick; uint32_t uspq; } tempo[4096]; static int ntempo;
static int division;
static double tick_sec(uint32_t tick)
{
	double s = 0; uint32_t at = 0, uspq = 500000;
	for (int i = 0; i < ntempo && tempo[i].tick <= tick; i++) {
		s += (double) (tempo[i].tick - at) * uspq / 1e6 / division;
		at = tempo[i].tick; uspq = tempo[i].uspq;
	}
	return s + (double) (tick - at) * uspq / 1e6 / division;
}

static void parse_tracks(const uint8_t *buf, size_t size, int ntracks, int pass)
{
	const uint8_t *p = buf + 14;
	int order = 0;
	for (int t = 0; t < ntracks && p + 8 <= buf + size; t++) {
		uint32_t len = be(p + 4, 4);
		const uint8_t *q = p + 8, *end = q + len;
		uint32_t tick = 0; uint8_t status = 0;
		while (q < end) {
			tick += vlq(&q);
			uint8_t b = *q;
			if (b == 0xff) {
				uint8_t type = q[1]; q += 2; uint32_t l = vlq(&q);
				if (pass == 0 && type == 0x51 && ntempo < 4096) {
					tempo[ntempo].tick = tick; tempo[ntempo].uspq = be(q, 3); ntempo++;
				}
				q += l;
			} else if (b == 0xf0 || b == 0xf7) {
				q++; uint32_t l = vlq(&q);
				if (pass == 1) {
					uint8_t *m = malloc(l + 1); m[0] = 0xf0; memcpy(m + 1, q, l);
					if (b == 0xf0) add(tick_sec(tick), m, l + 1, order++);
					else add(tick_sec(tick), q, l, order++);
					free(m);
				}
				q += l;
			} else {
				if (b & 0x80) { status = b; q++; }
				int n = ((status & 0xe0) == 0xc0) ? 1 : 2;
				uint8_t m[3] = { status, q[0], n > 1 ? q[1] : 0 };
				if (pass == 1) add(tick_sec(tick), m, (uint32_t) n + 1, order++);
				q += n;
			}
		}
		p = end;
	}
}

static int tempo_cmp(const void *a, const void *b)
{
	const uint32_t *x = a, *y = b; return (*x > *y) - (*x < *y);
}

int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: smf2mt32 in.mid out.wav romdir [tail-seconds]\n"); return 2; }
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
	fseek(f, 0, SEEK_END); size_t size = (size_t) ftell(f); rewind(f);
	uint8_t *buf = malloc(size); if (fread(buf, 1, size, f) != size) return 1; fclose(f);
	if (memcmp(buf, "MThd", 4)) { fprintf(stderr, "not a MIDI file\n"); return 1; }
	int ntracks = (int) be(buf + 10, 2); division = (int) be(buf + 12, 2);
	if (division & 0x8000) { fprintf(stderr, "SMPTE division not supported\n"); return 1; }
	parse_tracks(buf, size, ntracks, 0);
	qsort(tempo, (size_t) ntempo, sizeof tempo[0], tempo_cmp);
	parse_tracks(buf, size, ntracks, 1);
	qsort(ev, nev, sizeof *ev, cmp);

	mt32emu_report_handler_i none = { 0 };
	mt32emu_context c = mt32emu_create_context(none, NULL);
	char path[1024];
	snprintf(path, sizeof path, "%s/ctrl_mt32_1_07.rom", argv[3]);
	if (mt32emu_add_rom_file(c, path) < 0) { fprintf(stderr, "control ROM\n"); return 1; }
	snprintf(path, sizeof path, "%s/pcm_mt32.rom", argv[3]);
	if (mt32emu_add_rom_file(c, path) < 0) { fprintf(stderr, "PCM ROM\n"); return 1; }
	if (mt32emu_open_synth(c) != MT32EMU_RC_OK) { fprintf(stderr, "open synth\n"); return 1; }
	const uint32_t rate = mt32emu_get_actual_stereo_output_samplerate(c);

	FILE *o = fopen(argv[2], "wb"); if (!o) { perror(argv[2]); return 1; }
	uint8_t hdr[44] = { 0 }; fwrite(hdr, 1, 44, o);
	double tail = argc > 4 ? atof(argv[4]) : 3.0;
	uint64_t done = 0, total = (uint64_t) (((nev ? ev[nev - 1].sec : 0) + tail) * rate);
	int16_t out[2 * 1024];
	size_t i = 0;
	while (done < total) {
		while (i < nev && (uint64_t) (ev[i].sec * rate) <= done) {
			mt32emu_parse_stream(c, ev[i].data, ev[i].len);
			i++;
		}
		uint64_t next = i < nev ? (uint64_t) (ev[i].sec * rate) : total;
		uint32_t n = (uint32_t) (next - done); if (n > 1024) n = 1024; if (n == 0) n = 1;
		mt32emu_render_bit16s(c, out, n);
		fwrite(out, 4, n, o); done += n;
	}
	uint32_t bytes = (uint32_t) (done * 4);
	memcpy(hdr, "RIFF", 4); uint32_t v = 36 + bytes; memcpy(hdr + 4, &v, 4); memcpy(hdr + 8, "WAVEfmt ", 8);
	v = 16; memcpy(hdr + 16, &v, 4); uint16_t s = 1; memcpy(hdr + 20, &s, 2); s = 2; memcpy(hdr + 22, &s, 2);
	memcpy(hdr + 24, &rate, 4); v = rate * 4; memcpy(hdr + 28, &v, 4); s = 4; memcpy(hdr + 32, &s, 2);
	s = 16; memcpy(hdr + 34, &s, 2); memcpy(hdr + 36, "data", 4); memcpy(hdr + 40, &bytes, 4);
	rewind(o); fwrite(hdr, 1, 44, o); fclose(o);
	fprintf(stderr, "%zu events, %.1f s at %u Hz\n", nev, (double) done / rate, rate);
	mt32emu_close_synth(c); mt32emu_free_context(c);
	return 0;
}
