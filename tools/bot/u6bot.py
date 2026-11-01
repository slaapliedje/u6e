#!/usr/bin/env python3
"""u6bot.py: play u6e through Hatari's keyboard, lock-step with the game's bot reports.

The game, started with BOT.REQ in its folder, prints one '@S' line after each key it has handled
(docs/design/bot_harness.md). Game.key() sends one ST scancode through Hatari's command FIFO and
returns the parsed report whose key count has caught up. Dumps: actors() (F2), objects() (F3),
passmap() (F4, reads BOTPASS.BIN from the drive folder)."""
import os, re, select, subprocess, time

# ST scancodes (US layout): letters, digits, and the keys the game uses
SC = {c: v for c, v in zip('qwertyuiop', range(0x10, 0x1a))}
SC.update({c: v for c, v in zip('asdfghjkl', range(0x1e, 0x27))})
SC.update({c: v for c, v in zip('zxcvbnm', range(0x2c, 0x33))})
SC.update({c: v for c, v in zip('1234567890', range(0x02, 0x0c))})
SC.update({' ': 0x39, ',': 0x33, '.': 0x34, '/': 0x35, '-': 0x0c, "'": 0x28, ';': 0x27})
SHIFTED = {'<': ',', '>': '.', '?': '/', '!': '1', '"': "'", ':': ';'}
K = dict(ret=0x1c, esc=0x01, bksp=0x0e, up=0x48, down=0x50, left=0x4b, right=0x4d,
         f1=0x3b, f2=0x3c, f3=0x3d, f4=0x3e, f5=0x3f, space=0x39, shift=0x2a)
DIRS = {(0, -1): 'up', (0, 1): 'down', (-1, 0): 'left', (1, 0): 'right'}

def parse_state(line):
    body = line[3:]
    tail = {}
    for tag in (' typed=', ' conv=', ' msg='):            # free-text fields come last, in this order
        i = body.rfind(tag)
        if i >= 0: tail[tag.strip(' =')] = body[i + len(tag):]; body = body[:i]
    st = {}
    for tok in body.split():
        if '=' not in tok: continue
        k, v = tok.split('=', 1)
        if k == 'party':
            st[k] = [dict(zip(('id', 'hp', 'max', 'mp', 'lvl', 'psn'), map(int, m.replace('/', ':').split(':')))) for m in v.split(',') if m]
        elif k in ('verb', 'askc'): st[k] = '' if v == '-' else v
        else:
            try: st[k] = int(v)
            except ValueError: st[k] = v
    st.update(tail)
    st.setdefault('msg', ''); st.setdefault('conv', ''); st.setdefault('typed', '')
    return st

def kv(line):
    return {k: (int(v) if re.fullmatch(r'-?\d+', v) else v) for k, v in re.findall(r'(\w+)=(\S*)', line.split(' name=')[0])} | \
           ({'name': line.split(' name=', 1)[1]} if ' name=' in line else {})

class Game:
    def __init__(self, hddir, outdir, machine='falcon', fast=True, avi=False, log=None, timeout=120):
        self.hd, self.out = os.path.abspath(hddir), os.path.abspath(outdir)
        os.makedirs(self.out, exist_ok=True)
        for f in ('BOTPASS.BIN', 'BOTDOOR.TXT'):
            if os.path.exists(os.path.join(self.hd, f)): os.unlink(os.path.join(self.hd, f))
        open(os.path.join(self.hd, 'BOT.REQ'), 'w').write('n')
        self.fifo = os.path.join(self.out, 'fifo')
        if os.path.exists(self.fifo): os.unlink(self.fifo)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        env.pop('DISPLAY', None); env.pop('WAYLAND_DISPLAY', None)
        if machine == 'falcon':
            mach = ['--machine', 'falcon', '--memsize', '4', '--ttram', '0', '--dsp', 'none',
                    '--tos', '/usr/share/hatari/TOSv4.04.img', '--auto', 'C:\\GAMEF.PRG']
        else:
            mach = ['--machine', 'tt', '--memsize', '4', '--ttram', '0',
                    '--tos', '/usr/share/hatari/TOS v3.06 (1991)(Atari)(GB)[TT].img', '--auto', 'C:\\GAME.PRG']
        args = ['stdbuf', '-oL', 'hatari'] + mach + ['--monitor', 'vga', '--drive-a', 'off', '--drive-b', 'off',
                '--harddrive', self.hd, '--fast-boot', 'on', '--fast-forward', 'on' if fast else 'off',
                '--sound', 'off', '--statusbar', 'off', '--drive-led', 'off', '--confirm-quit', 'off',
                '--natfeats', 'on', '--conout', '2', '--screenshot-dir', self.out, '--cmd-fifo', self.fifo]
        if avi: args += ['--avirecord', '--avi-vcodec', 'png', '--avi-file', os.path.join(self.out, 'run.avi')]
        self.log = open(log or os.path.join(self.out, 'console.log'), 'w')
        self.p = subprocess.Popen(args, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
        self.buf = b''; self.keys = 0; self.state = None
        self.watch = None                                  # called with every report (dumps included)
        t0 = time.time()
        while not os.path.exists(self.fifo):
            if time.time() - t0 > 20: raise RuntimeError('no hatari fifo')
            time.sleep(0.05)
        self.state = self.wait(lambda l: l.startswith('@S'), timeout)
        self.keys = self.state['k']

    def lines(self, timeout):
        """yield console lines until timeout (seconds without a complete line)"""
        while True:
            while b'\n' in self.buf:
                ln, self.buf = self.buf.split(b'\n', 1)
                s = ln.decode('latin-1').rstrip('\r')
                self.log.write(s + '\n'); self.log.flush()
                yield s
            r, _, _ = select.select([self.p.stdout], [], [], timeout)
            if not r: return
            d = os.read(self.p.stdout.fileno(), 65536)
            if not d: raise RuntimeError('hatari exited')
            self.buf += d

    def wait(self, pred, timeout=60):
        for s in self.lines(timeout):
            if pred(s): return parse_state(s) if s.startswith('@S') else s
        raise TimeoutError('no matching console line within %ss' % timeout)

    def cmd(self, s):
        # opened per command: Hatari polls the FIFO non-blocking and prints an error on every empty
        # read while a writer holds it open
        with open(self.fifo, 'w') as f: f.write(s + '\n')

    def key(self, code, timeout=60, shift=False):
        """press one key and wait for the report that covers it; the console lines printed in
        between (dumps) are kept in self.got"""
        if isinstance(code, str): code = K[code] if code in K else SC[code]
        if shift: self.cmd('hatari-event keydown 0x%02x' % K['shift']); time.sleep(0.02)
        self.cmd('hatari-event keypress 0x%02x' % code)   # hex: a lone digit is taken as that character, and '08' is not octal
        if shift: time.sleep(0.02); self.cmd('hatari-event keyup 0x%02x' % K['shift'])
        self.keys += 1
        want, self.got = self.keys, []
        for s in self.lines(timeout):
            if s.startswith('@S') and int(re.search(r' k=(\d+)', s).group(1)) >= want:
                self.state = parse_state(s); self.keys = self.state['k']
                if self.watch: self.watch(self.state)
                return self.state
            self.got.append(s)
        raise TimeoutError('no report for key %d within %ss' % (code, timeout))

    def char(self, c):
        if c in SHIFTED: return self.key(SC[SHIFTED[c]], shift=True)
        if c.isupper(): return self.key(SC[c.lower()], shift=True)
        return self.key(SC[c])

    def type(self, text, enter=True):
        for c in text: self.char(c)
        return self.key('ret') if enter else self.state

    def dump(self, key, end):
        """press a dump key and collect its lines up to the end marker: a report can arrive first (a key
        that lands as a camp or a walk finishes is counted in that report), so read on until the marker"""
        for _ in range(3):                                   # a key swallowed by a busy spell: press again
            self.key(key)
            if end in self.got: return
            got = self.got
            for s in self.lines(5):
                got.append(s)
                if s.startswith('@S'): self.state = parse_state(s)
                if s == end: self.got = got; return
        raise TimeoutError('dump %s: no %r' % (key, end))

    def actors(self):
        self.dump('f2', '@A end'); return [kv(l) for l in self.got if l.startswith('@A ') and l != '@A end']

    def objects(self):
        self.dump('f3', '@O end'); rows = [kv(l) for l in self.got if l.startswith(('@O ', '@I ')) and l != '@O end']
        self.ready = [kv(l) for l in self.got if l.startswith('@E ')]           # readied things: id slot obj
        return [r for r in rows if 'i' in r], [r for r in rows if 'row' in r]

    def passmap(self):
        self.key('f4', timeout=180)
        s = next((l for l in self.got if l.startswith('@P')), '')
        m = re.search(r'span=(\d+)', s)
        if not m: raise RuntimeError('pass dump failed: ' + s)
        span = int(m.group(1)); data = open(os.path.join(self.hd, 'BOTPASS.BIN'), 'rb').read()
        doors = [tuple(map(int, l.split())) for l in open(os.path.join(self.hd, 'BOTDOOR.TXT')) if l.strip()]
        return span, bytearray(data), doors

    def shot(self, name=None):
        import glob
        before = set(glob.glob(self.out + '/*.png')); self.cmd('hatari-shortcut screenshot')
        for _ in range(100):
            time.sleep(0.05); new = set(glob.glob(self.out + '/*.png')) - before
            if new:
                f = new.pop(); time.sleep(0.2)
                if name: os.replace(f, os.path.join(self.out, name)); f = os.path.join(self.out, name)
                return f

    def quit(self):
        try: self.cmd('hatari-shortcut quit'); self.p.wait(timeout=10)
        except Exception: self.p.kill(); self.p.wait()
        try: os.unlink(os.path.join(self.hd, 'BOT.REQ'))
        except OSError: pass
