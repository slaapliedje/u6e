#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""vidrun.py <timeline> <outdir>: records u6e on a stock 4 MB Falcon in Hatari, in real time.
Starts the private MIDI recorder (midirec), then Hatari with --avirecord and a command FIFO,
and plays the timeline: one command per line, '<seconds> <cmd> [args]', seconds from launch.
  key <scancode> [n] [gap]   press a key n times (ST scancode; 57 space, 28 return, 72/80/75/77 arrows)
  char <c>                   press the key for an ASCII letter (hatari-event keypress <c>)
  mouse <dx> <dy>            move the pointer with Alt+arrows (16 px per press)
  click                      left click (Alt+Insert)
  shot <name>                screenshot into outdir/shots
  waitmenu <every> <code> <timeout>  press <code> every <every> s (intro pages) until a screenshot
                             shows the main menu; later lines' times are relative to that moment
  quit                       end the run
Writes outdir/run.avi, outdir/music.mid, outdir/times.txt (wall-clock of recorder zero and Hatari launch)."""
import glob, os, subprocess, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from menudet import is_menu
HERE = os.path.dirname(os.path.abspath(__file__))
tl, out = sys.argv[1], os.path.abspath(sys.argv[2])
os.makedirs(out + '/shots', exist_ok=True)
fifo = out + '/fifo'
if os.path.exists(fifo): os.unlink(fifo)
rec = subprocess.Popen([HERE + '/midirec', out + '/music.mid', out + '/rec.t0'])
time.sleep(0.5)
env = dict(os.environ, SDL_VIDEODRIVER='dummy'); env.pop('DISPLAY', None); env.pop('WAYLAND_DISPLAY', None)
t_launch = time.time()
hat = subprocess.Popen(['hatari', '-c', HERE + '/hatari-midi.cfg', '--machine', 'falcon', '--memsize', '4',
    '--ttram', '0', '--dsp', 'none', '--monitor', 'vga', '--tos', '/usr/share/hatari/TOSv4.04.img',
    '--drive-a', 'off', '--drive-b', 'off', '--harddrive', os.environ['HDDIR'], '--auto', 'C:\\GAMEF.PRG',
    '--fast-boot', 'on', '--fast-forward', 'off', '--frameskips', '0', '--sound', '48000',
    '--statusbar', 'off', '--drive-led', 'off', '--confirm-quit', 'off', '--conout', '2',
    '--avirecord', '--avi-vcodec', 'png', '--png-level', '1', '--avi-file', out + '/run.avi',
    '--screenshot-dir', out + '/shots', '--cmd-fifo', fifo], env=env,
    stdout=open(out + '/hatari.log', 'w'), stderr=subprocess.STDOUT)
while not os.path.exists(fifo): time.sleep(0.05)
def cmd(s):
    with open(fifo, 'w') as f: f.write(s + '\n')
def press(code, n=1, gap=0.15):
    for _ in range(n): cmd(f'hatari-event keypress {code}'); time.sleep(gap)
def grab():
    before = set(glob.glob(out + '/shots/*.png')); cmd('hatari-shortcut screenshot')
    for _ in range(40):
        time.sleep(0.05); new = set(glob.glob(out + '/shots/*.png')) - before
        if new: time.sleep(0.2); return new.pop()
def load(path):
    """timeline lines, each segment (split at waitmenu) sorted by time"""
    segs, cur = [], []
    for raw in open(path):
        f = raw.split('#')[0].split()
        if not f: continue
        cur.append((float(f[0]), f[1], f[2:]))
        if f[1] == 'waitmenu': segs.append(cur); cur = []
    segs.append(cur)
    return [e for sg in segs for e in sorted(sg, key=lambda e: (e[0], e[1] == 'waitmenu'))]
base = t_launch
for at, op, args in load(tl):
    while time.time() - base < at: time.sleep(0.01)
    if hat.poll() is not None: break
    if op == 'key': press(args[0], int(args[1]) if len(args) > 1 else 1, float(args[2]) if len(args) > 2 else 0.15)
    elif op == 'char': cmd(f'hatari-event keypress {args[0]}')
    elif op == 'mouse':
        dx, dy = int(args[0]), int(args[1]); cmd('hatari-event keydown 56')
        for n, k in ((dx, 77 if dx > 0 else 75), (dy, 80 if dy > 0 else 72)):
            for _ in range(abs(n) // 16): cmd(f'hatari-event keypress {k}'); time.sleep(0.08)
        cmd('hatari-event keyup 56')
    elif op == 'click': cmd('hatari-event keydown 56'); cmd('hatari-event keypress 82'); time.sleep(0.1); cmd('hatari-event keyup 56')
    elif op == 'shot': cmd('hatari-shortcut screenshot')
    elif op == 'waitmenu':
        every, code, tmo = float(args[0]), args[1], float(args[2]); nxt = time.time() + every; end = time.time() + tmo
        while time.time() < end:
            f = grab()
            if f and is_menu(f): os.unlink(f); break
            if f: os.unlink(f)
            if time.time() >= nxt: press(code); nxt += every
            time.sleep(0.5)
        base = time.time()
        print('menu at %.2f s' % (base - t_launch), flush=True)
    elif op == 'quit': cmd('hatari-shortcut quit'); break
    print('%7.2f %s %s' % (time.time() - t_launch, op, ' '.join(args)), flush=True)
try: hat.wait(timeout=15)
except subprocess.TimeoutExpired: hat.kill(); hat.wait()
rec.send_signal(2); rec.wait()
open(out + '/times.txt', 'w').write('rec_t0 %s\nhatari_launch %.6f\n' % (open(out + '/rec.t0').read().strip(), t_launch))
