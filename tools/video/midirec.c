/* midirec <out.mid> [startfile]: private ALSA sequencer recorder.
 * Creates client "u6e-rec" with one writable port, so Hatari (PortMidi) can send straight
 * to it without the shared Midi Through port. Records until SIGINT/SIGTERM and writes a
 * type-0 SMF with 1 tick = 1 ms (division 500, tempo 500000). Writes the CLOCK_REALTIME
 * of time zero (seconds, %.6f) to startfile if given, for audio/video alignment. */
#include <alsa/asoundlib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static volatile sig_atomic_t stop;
static void onsig(int s) { (void) s; stop = 1; }
static unsigned char *buf; static size_t n, cap;
static void put(const void *p, size_t l) {
	if (n + l > cap) buf = realloc(buf, cap = (n + l) * 2 + 4096);
	memcpy(buf + n, p, l); n += l;
}
static void vlq(unsigned v) {
	unsigned char t[5]; int i = 4; t[i] = v & 0x7f;
	while (v >>= 7) t[--i] = 0x80 | (v & 0x7f);
	put(t + i, 5 - i);
}
int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: midirec out.mid [startfile]\n"); return 2; }
	snd_seq_t *seq;
	if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) { fprintf(stderr, "seq open\n"); return 1; }
	snd_seq_set_client_name(seq, "u6e-rec");
	int port = snd_seq_create_simple_port(seq, "u6e-rec in",
		SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
	snd_midi_event_t *dec; snd_midi_event_new(65536, &dec); snd_midi_event_no_status(dec, 1);
	signal(SIGINT, onsig); signal(SIGTERM, onsig);
	struct timespec t0; clock_gettime(CLOCK_MONOTONIC, &t0);
	struct timespec r0; clock_gettime(CLOCK_REALTIME, &r0);
	if (argc > 2) { FILE *f = fopen(argv[2], "w"); fprintf(f, "%.6f\n", r0.tv_sec + r0.tv_nsec / 1e9); fclose(f); }
	fprintf(stderr, "u6e-rec ready: client %d port %d\n", snd_seq_client_id(seq), port);
	unsigned last = 0, events = 0;
	unsigned char tempo[] = { 0xff, 0x51, 3, 0x07, 0xa1, 0x20 }; vlq(0); put(tempo, 6);
	struct pollfd pfd[4]; int np = snd_seq_poll_descriptors(seq, pfd, 4, POLLIN);
	while (!stop) {
		if (poll(pfd, np, 200) <= 0) continue;
		snd_seq_event_t *ev;
		while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
			struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
			unsigned ms = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
			unsigned char m[65536]; long l;
			if (ev->type == SND_SEQ_EVENT_SYSEX) {
				unsigned char *d = ev->data.ext.ptr; l = ev->data.ext.len;
				vlq(ms - last); last = ms;
				if (l && d[0] == 0xf0) { unsigned char f0 = 0xf0; put(&f0, 1); vlq(l - 1); put(d + 1, l - 1); }
				else { unsigned char f7 = 0xf7; put(&f7, 1); vlq(l); put(d, l); }
				events++;
			} else if ((l = snd_midi_event_decode(dec, m, sizeof m, ev)) > 0) {
				vlq(ms - last); last = ms; put(m, l); events++;
			}
			if (snd_seq_event_input_pending(seq, 0) <= 0) break;
		}
	}
	unsigned char eot[] = { 0xff, 0x2f, 0 }; vlq(0); put(eot, 3);
	FILE *f = fopen(argv[1], "wb");
	unsigned char hdr[] = { 'M','T','h','d',0,0,0,6, 0,0, 0,1, 0x01,0xf4, 'M','T','r','k',
		(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char) n };
	fwrite(hdr, 1, sizeof hdr, f); fwrite(buf, 1, n, f); fclose(f);
	fprintf(stderr, "u6e-rec: %u events, %.1f s\n", events, last / 1000.0);
	return 0;
}
