#!/usr/bin/env python3
"""run.py <drive folder> <out dir> [segment ...]: the bot's walkthrough, segment by segment.
Each segment plays part of the game with the real keyboard; issues.jsonl and transcripts.json
collect what looked wrong. Segments: castle, bridge, britain (default: all, in order)."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from u6bot import Game
from play import Player, keywords
from nav import VDIR

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF = os.environ.get('U6REF', os.path.join(ROOT, 'refdata', 'dos', 'u6'))
PARTY = (1, 2, 3, 4)

def seg_castle(p, kw):
    """Lord British (the Compendium questions, every topic), the party, everyone in the castle"""
    name, words = kw[5]
    p.talk_to(5, [w for w in words if w not in ('join', 'leav', 'bye')], name)
    for m in PARTY[1:]:
        name, words = kw[m]; p.talk_to(m, [w for w in words if w not in ('join', 'leav', 'bye')], name)
    p.sweep((286, 336, 330, 386), kw, done={5}, party=PARTY)

def seg_bridge(p, kw):
    """the crank opens the drawbridge; walk across the moat"""
    p.nav.goto(303, 384, adj=True); x, y = p.nav.pos()
    p.key('u'); st = p.key(VDIR[(303 - x, 384 - y)], 'crank')
    if 'drawbridge' not in st['msg']: p.issue('bridge', 'crank said %r' % st['msg'])
    p.nav.refresh(); p.nav.goto(307, 395)

def seg_britain(p, kw):
    p.sweep((270, 388, 360, 470), kw, done=set(p.transcripts), party=PARTY)

SEGS = dict(castle=seg_castle, bridge=seg_bridge, britain=seg_britain)

def main():
    hd, out = sys.argv[1], os.path.abspath(sys.argv[2]); segs = sys.argv[3:] or list(SEGS)
    os.makedirs(out, exist_ok=True)
    logf = open(os.path.join(out, 'run.log'), 'a')
    def log(s):
        line = '%s %s' % (time.strftime('%H:%M:%S'), s); print(line, flush=True); logf.write(line + '\n'); logf.flush()
    kw = keywords(REF)
    g = Game(hd, out); p = Player(g, out, REF, log)
    t0 = time.time()
    try:
        for s in segs:
            log('== segment %s' % s); ts = time.time()
            try: SEGS[s](p, kw)
            except Exception as e: p.issue('crash', '%s: %s: %s' % (s, type(e).__name__, e)); raise
            st = g.state
            log('== %s done in %.0fs: L%d (%d,%d) %02d:%02d day %d keys %d issues %d' % (s, time.time() - ts, st['L'], st['X'],
                st['Y'], st['min'] // 60, st['min'] % 60, st['day'], st['k'], len(p.issues)))
    finally:
        json.dump({str(k): v for k, v in p.transcripts.items()}, open(os.path.join(out, 'transcripts.json'), 'w'), indent=1)
        g.shot('final.png'); g.quit()
        log('run %.0fs, %d issues' % (time.time() - t0, len(p.issues)))

if __name__ == '__main__': main()
