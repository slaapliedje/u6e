#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""se.py: Savage Empire bot runs (docs/design/bot_harness.md), on the same Game/Player as U6's run.py.

  se.py OUT [segment...]      segments: smoke, talk (default: smoke talk)

SE_START=x,y starts the party there (a pack built with that start). The drive folder (BOT_SE_HD, default build/se-bot) is rebuilt from build/SE.PAK and build/gamef.prg
each run: `make world-se build/gamef.prg` first. smoke boots SE and checks that walking, looking,
talking and save/restore answer; talk speaks every keyword of every scripted actor it can walk to
(BOTTOUGH.TXT: the party takes no damage, so the jungle does not end the sweep)."""
import json, os, shutil, signal, subprocess, sys, time
from u6bot import Game
from play import Player, keywords, SKIP_WORDS
from nav import NavError

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
REF = os.path.join(ROOT, os.environ.get('SE_REFDATA', 'refdata/dos/savage/SAVAGE'))
VM_ERRORS = ('unknown command', 'control char', 'error')        # the interpreter's own complaints (converse.md §6c)

def prepare(hd, tough, start=None):
    """the drive: build/SE.PAK, or with start='x,y' a pack whose party starts there (mkworld's start, as
    make world-se SE_X= SE_Y=)"""
    if os.path.isdir(hd): shutil.rmtree(hd)
    os.makedirs(hd)
    b = os.path.join(ROOT, 'build')
    for src, dst in (('SE.PAK', 'U6.PAK'), ('SAVE.SE', 'SAVE.000'), ('gamef.prg', 'GAMEF.PRG'), ('NEWDESK.INF', 'NEWDESK.INF')):
        shutil.copyfile(os.path.join(b, src), os.path.join(hd, dst))
    if start:
        x, y = start.split(',')
        subprocess.run([os.path.join(b, 'mkworld'), REF, os.path.join(hd, 'U6.PAK'), x, y] +
                       [os.path.join(b, f) for f in ('se.objblk', 'se.objlist', 'se.inv')],
                       env=dict(os.environ, MKWORLD_GAME='savage'), check=True, capture_output=True)
    if tough: open(os.path.join(hd, 'BOTTOUGH.TXT'), 'w').write('1')

def vm_check(p, aid, out):
    for w, reply in out:
        low = reply.lower()
        for e in VM_ERRORS:
            if e in low: p.issue('talk-vm', '%d: %r gave %r' % (aid, w, reply[-160:]), shot=False)

# ---- smoke ------------------------------------------------------------------------------------
def smoke(p, kw):
    g = p.g; st = g.state
    ids = [m['id'] for m in st['party']]
    if 1 not in ids or st['L'] != 0: p.issue('smoke', 'boot: party %s on level %d' % (ids, st['L']))
    x0, y0 = p.nav.pos(); moved = 0
    for k, back in (('up', 'down'), ('left', 'right'), ('down', 'up'), ('right', 'left')):
        st = p.key(k, 'walk')
        if (st['X'], st['Y']) != (x0, y0): moved += 1; p.key(back, 'walk back')
    if not moved: p.issue('smoke', 'walk: no direction moved from (%d,%d)' % (x0, y0))
    for d in ('up', 'down', 'left', 'right'):                  # L)ook: something answers
        p.key('l'); p.key(d); st = p.key('ret', 'look')          # the crosshair a step that way, Enter
        if not st['msg'].strip(): p.issue('smoke', 'look %s: no text' % d)
        p.idle()
    party = set(ids)                                           # T)alk: the nearest scripted actor not in the party
    x, y = p.nav.pos()
    near = sorted((a for a in g.actors() if a['id'] in kw and a['id'] not in party and not a['hostile']),
                  key=lambda a: abs(a['x'] - x) + abs(a['y'] - y))
    if near:
        a = near[0]; name = kw[a['id']][0]
        out = p.talk_to(a['id'], ['name', 'job'], name)
        if out is None: p.issue('smoke', 'talk: could not talk to %d %s' % (a['id'], name))
        else: vm_check(p, a['id'], out)
    else: p.issue('smoke', 'talk: no scripted actor in the actor dump')
    st = g.state; here = (st['L'], st['X'], st['Y'], st['min'])  # S)ave slot 1, walk off, Ctrl-R slot 1: back where saved
    p.idle(); p.key('s', 'the slot page'); p.key('1', 'slot 1'); st = p.key('ret', 'save')
    saved = (st['L'], st['X'], st['Y'])
    p.log('  save: %r at L%d (%d,%d)' % (st['msg'], *saved))
    for k in ('up', 'up', 'left', 'left'): p.key(k, 'walk off')
    g.key('r', ctrl=True); p.key('1', 'slot 1'); st = p.key('ret', 'restore')
    p.log('  restore: %r at L%d (%d,%d)' % (st['msg'], st['L'], st['X'], st['Y']))
    if (st['L'], st['X'], st['Y']) != saved:
        p.issue('smoke', 'save/restore: saved at %s, restored to L%d (%d,%d): %r' % (saved, st['L'], st['X'], st['Y'], st['msg']))
    p.nav.refresh()

# ---- talk sweep -------------------------------------------------------------------------------
def talk(p, kw):
    g = p.g; party = {m['id'] for m in g.state['party']}
    todo = {aid for aid in kw if aid not in party and aid != 1}
    talked, unreached, elsewhere = [], [], []
    while todo:
        here = []                                               # who stands on this level now, nearest first
        for aid in sorted(todo):
            w = g.where(aid)
            if w is None or w[2] != g.state['L']: elsewhere.append(aid); todo.discard(aid); continue
            here.append((aid, w))
        if not here: break
        x, y = p.nav.pos()
        aid, w = min(here, key=lambda t: abs(t[1][0] - x) + abs(t[1][1] - y))
        todo.discard(aid)
        name, words = kw[aid]
        words = [wd for wd in dict.fromkeys(words) if wd not in SKIP_WORDS and len(wd) > 1]
        p.log('talk %d %s at (%d,%d) (%d words)' % (aid, name, w[0], w[1], len(words)))
        try: out = p.talk_to(aid, words, name)
        except NavError as e: p.log('  %s' % e); out = None
        if out is None: unreached.append(aid); continue
        talked.append(aid); vm_check(p, aid, out)
    p.log('talk sweep: %d talked, %d not reached %s, %d on other levels %s' % (len(talked), len(unreached), unreached,
          len(elsewhere), elsewhere))

SEGS = dict(smoke=smoke, talk=talk)

def main():
    out = os.path.abspath(sys.argv[1]); segs = sys.argv[2:] or ['smoke', 'talk']
    hd = os.path.abspath(os.environ.get('BOT_SE_HD', os.path.join(ROOT, 'build', 'se-bot')))
    os.makedirs(out, exist_ok=True)
    prepare(hd, tough='talk' in segs, start=os.environ.get('SE_START'))
    logf = open(os.path.join(out, 'run.log'), 'a')
    def log(s):
        line = '%s %s' % (time.strftime('%H:%M:%S'), s); print(line, flush=True); logf.write(line + '\n'); logf.flush()
    def stop(sig, frame): raise SystemExit('signal %d' % sig)
    signal.signal(signal.SIGTERM, stop); signal.signal(signal.SIGHUP, stop)
    kw = keywords(REF)
    g = Game(hd, out, machine='falcon'); p = Player(g, out, REF, log)
    t0 = time.time()
    try:
        for s in segs:
            log('== segment %s' % s); ts = time.time()
            try: SEGS[s](p, kw)
            except Exception as e: p.issue('crash', '%s: %s: %s' % (s, type(e).__name__, e)); raise
            st = g.state
            log('== %s done in %.0fs: L%d (%d,%d) keys %d issues %d' % (s, time.time() - ts, st['L'], st['X'], st['Y'], st['k'], len(p.issues)))
    finally:
        try:
            json.dump({str(k): v for k, v in p.transcripts.items()}, open(os.path.join(out, 'transcripts.json'), 'w'), indent=1)
            g.shot('final.png')
        finally: g.quit()
        log('run %.0fs, %d issues' % (time.time() - t0, len(p.issues)))
    return 1 if p.issues else 0

if __name__ == '__main__': sys.exit(main())
