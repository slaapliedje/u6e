#!/usr/bin/env python3
"""finish.py <rundir> [out.mp4]: turns a vidrun.py recording into a YouTube-ready MP4.
1. renders music.mid through libmt32emu (MT-32 1.07 ROMs) -> music.wav
2. finds two sync anchors: the menu's fade-in in the AVI and the menu song's cue in the MIDI
   (the all-notes-off burst nearest to it), and the intro song's cue vs. the boot fade-out;
   the audio offset comes from the intro anchor, the menu anchor reports drift
3. muxes: 640x480 -> 1280x960 nearest-neighbour, padded to 1440x1080; H.264 CRF 16 + AAC 320k."""
import os, subprocess, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
run = os.path.abspath(sys.argv[1]); outmp4 = sys.argv[2] if len(sys.argv) > 2 else run + '/u6e-falcon.mp4'
ROMS = os.path.expanduser('~/.local/share/mt32-roms')
avi, mid, wav = run + '/run.avi', run + '/music.mid', run + '/music.wav'
subprocess.run([HERE + '/smf2mt32', mid, wav, ROMS, '3'], check=True)
times = dict(l.split() for l in open(run + '/times.txt'))
rec_lead = float(times['hatari_launch']) - float(times['rec_t0'])   # recorder zero precedes launch

# --- video: per-frame 64x48 RGB thumbnails
fps_s = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v', '-show_entries', 'stream=r_frame_rate',
                        '-of', 'csv=p=0', avi], capture_output=True, text=True).stdout.strip()
a, b = fps_s.split('/'); fps = int(a) / int(b)
raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', avi, '-vf', 'scale=64:48:flags=area', '-f', 'rawvideo',
                      '-pix_fmt', 'rgb24', '-'], capture_output=True).stdout
fr = np.frombuffer(raw, np.uint8).reshape(-1, 48, 64, 3).astype(np.int32)
s = fr.sum(axis=3)
left_black = (s[:, 18:, 0:6] < 30).all(axis=(1, 2)); gap_black = (s[:, 17:20, :] < 30).all(axis=(1, 2))
blue = (fr[:, 6:14, :, 2] > fr[:, 6:14, :, 0] + 20).sum(axis=(1, 2)); panel = (s[:, 21:44, 14:50] > 300).sum(axis=(1, 2))
menu = left_black & gap_black & (blue > 30) & (panel > 500)
mean = s.mean(axis=(1, 2)) / 3
first_menu = int(np.argmax(menu))
k = first_menu
while k > 0 and mean[k - 1] > 1.0: k -= 1                  # back up to the first non-black frame of the fade-in
menu_v = k / fps
# boot anchor: the fade to black that precedes the intro (first frame after 5 s whose mean drops to ~0)
boot = next(i for i in range(int(5 * fps), len(mean)) if mean[i] < 1.0)
j = boot
while j > 0 and mean[j - 1] < mean[j] + 200 and mean[j - 1] > mean[j]: j -= 1   # start of that fade
boot_v = j / fps

# --- MIDI: cues = all-notes-off bursts (CC 123) and the first event
d = open(mid, 'rb').read(); p = 22; t = 0; st = 0; cues = []; first = None
def vlq():
    global p; v = 0
    while True:
        c = d[p]; p += 1; v = v << 7 | c & 0x7f
        if not c & 0x80: return v
while p < len(d):
    t += vlq(); bb = d[p]
    if bb == 0xff: p += 2; l = vlq(); p += l; continue
    if bb in (0xf0, 0xf7): p += 1; l = vlq(); p += l; continue
    if bb & 0x80: st = bb; p += 1
    n = 1 if st & 0xf0 in (0xc0, 0xd0) else 2
    if first is None: first = t
    if st & 0xf0 == 0xb0 and d[p] == 123 and (not cues or t - cues[-1] > 500): cues.append(t)
    p += n
cue_s = [c / 1000 for c in cues]
# expected MIDI time of an event at video time v is v + rec_lead + latency; find the menu cue
menu_m = min(cue_s, key=lambda c: abs(c - (menu_v + rec_lead)))
intro_m = first / 1000
offset = intro_m - boot_v      # the intro song is cued at the start of the boot fade (steady to ~20 ms across runs)
drift = (menu_m - offset) - menu_v   # the menu song starts up to ~0.1 s after its fade-in begins (disk load)
print('fps %.4f  frames %d  menu fade-in %.3f s  boot fade %.3f s' % (fps, len(mean), menu_v, boot_v))
print('MIDI: first event %.3f s  menu cue %.3f s  recorder lead %.3f s' % (intro_m, menu_m, rec_lead))
print('audio offset %.3f s (AVI starts %.3f s after launch)  menu-anchor residual %+.3f s' % (offset, offset - rec_lead, drift))
open(run + '/sync.txt', 'w').write('offset %.3f\nresidual %.3f\n' % (offset, drift))

dur = len(mean) / fps
vf = 'scale=1280:960:flags=neighbor,pad=1440:1080:80:60:black,format=yuv420p'
af = 'atrim=start=%.3f,asetpts=PTS-STARTPTS,aresample=48000,apad,atrim=end=%.3f' % (offset, dur)
subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', avi, '-i', wav, '-filter_complex',
                '[0:v]%s[v];[1:a]%s[a]' % (vf, af), '-map', '[v]', '-map', '[a]',
                '-c:v', 'libx264', '-preset', 'slow', '-crf', '16', '-r', '60', '-g', '60',
                '-c:a', 'aac', '-b:a', '320k', '-movflags', '+faststart', '-t', '%.3f' % dur, outmp4], check=True)
print('wrote', outmp4)
