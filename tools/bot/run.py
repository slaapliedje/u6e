#!/usr/bin/env python3
"""run.py <drive folder> <out dir> [segment ...]: the bot's walkthrough, segment by segment.
Each segment plays part of the game with the real keyboard; issues.jsonl and transcripts.json
collect what looked wrong. Segments: castle, bridge, britain, shop, healer, cave, orb, shrine (default: all, in order)."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from u6bot import Game
from play import Player, keywords, Danger
from nav import VDIR

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
REF = os.environ.get('U6REF', os.path.join(ROOT, 'refdata', 'dos', 'u6'))
PARTY = (1, 2, 3, 4)

def seg_castle(p, kw):
    """the opening gargoyles; Lord British (the Compendium questions, every topic), the party, everyone in the castle"""
    if p.g.state['foes'] and p.foes(): p.fight()
    name, words = kw[5]
    p.talk_to(5, [w for w in words if w not in ('join', 'leav', 'bye') and len(w) > 1], name)
    for m in PARTY[1:]:
        name, words = kw[m]; p.talk_to(m, [w for w in words if w not in ('join', 'leav', 'bye') and len(w) > 1], name)
    p.sweep((286, 336, 330, 386), kw, done={5}, party=PARTY)

def seg_bridge(p, kw):
    """the crank opens the drawbridge; walk across the moat"""
    p.nav.goto(303, 384, adj=True); x, y = p.nav.pos()
    p.key('u'); st = p.key(VDIR[(303 - x, 384 - y)], 'crank')
    if 'drawbridge' not in st['msg']: p.issue('bridge', 'crank said %r' % st['msg'])
    p.nav.refresh(); p.nav.goto(307, 395)

def seg_britain(p, kw):
    p.sweep((270, 388, 360, 470), kw, done=set(p.transcripts), party=PARTY)

def pack_qty(p, obj_n):
    return sum(r['qty'] or 1 for r in p.g.objects()[1] if r['obj'] == obj_n)

def seg_shop(p, kw):
    """buy 4 torches from Efram (22) at 3 gold each: the gold and the pack must follow; then ask
    for more than the gold covers: he sells what it buys"""
    g0, t0 = p.g.state['gold'], pack_qty(p, 90)
    out = p.talk_to(22, ['buy', 'torc', '1', '4'], 'Efram')          # which of you: 1, how many: 4
    if out is None: p.issue('shop', 'could not reach Efram'); return
    g1, t1 = p.g.state['gold'], pack_qty(p, 90)
    p.log('  Efram: gold %d -> %d, torches %d -> %d' % (g0, g1, t0, t1))
    if (g0 - g1, t1 - t0) != (12, 4): p.issue('shop', '4 torches at 3: gold %d -> %d, torches %d -> %d (%r)' % (g0, g1, t0, t1, out[-2:]))
    if 'Torch\\' in ' '.join(r for _, r in out): p.issue('shop', 'plural marker printed: %r' % out[-2:])
    many = g1 // 3 + 5                                                 # more than the purse covers
    out = p.talk_to(22, ['buy', 'torc', '1', str(many)], 'Efram')
    g2, t2 = p.g.state['gold'], pack_qty(p, 90)
    p.log('  Efram, %d asked: gold %d -> %d, torches %d -> %d' % (many, g1, g2, t1, t2))
    if g2 < 0 or g1 - g2 != 3 * (t2 - t1) or g2 >= 3: p.issue('shop', 'asking %d torches with %d gold: gold %d, torches +%d' % (many, g1, g2, t2 - t1))

def seg_healer(p, kw):
    """Tiberius (26) heals in his shop 9-18 (worktype 0x90): Resurrect with nobody dead says so;
    Heal mends the chosen wounded member to full for 30 gold, free when the purse is short,
    and says all are well when nobody is hurt"""
    a = p.actor(26)
    if a and a.get('wt') != 0x90: p.log('  Tiberius not at work (wt=%s): skipped' % a.get('wt')); return
    hurt = [m['id'] for m in p.g.state['party'] if 0 < m['hp'] < m['max']]
    g0 = p.g.state['gold']
    out = p.talk_to(26, ['resu', 'heal', '1', 'y'], 'Tiberius', yes=True)
    if out is None: p.issue('healer', 'could not reach Tiberius'); return
    text = ' '.join(r for _, r in out)
    if 'none of thy party is dead' not in text.lower(): p.issue('healer', 'resurrect with nobody dead: %r' % out[1:2])
    g1 = p.g.state['gold']; still = [m['id'] for m in p.g.state['party'] if 0 < m['hp'] < m['max']]
    p.log('  Tiberius: hurt %s -> %s, gold %d -> %d' % (hurt, still, g0, g1))
    if not hurt:
        if 'art well' not in text and 'are well' not in text: p.issue('healer', 'nobody hurt, no "well": %r' % out[-3:])
    elif len(still) != len(hurt) - 1: p.issue('healer', 'heal mended %d of %s (still %s)' % (len(hurt) - len(still), hurt, still))
    elif g0 - g1 != (30 if g0 >= 30 else 0): p.issue('healer', 'heal cost %d with %d gold' % (g0 - g1, g0))

CAVE = (185, 436)                                     # the nearest cave west of Britain that can be walked to (obj 326)

def explore(p, n=6, seed=1):
    """walk to n far-apart reachable tiles of this level, fighting on the way"""
    import random; rnd = random.Random(seed)
    p.nav.refresh(); x, y = p.nav.pos(); S = p.nav.span
    seen, q = {(x, y)}, [(x, y)]
    for cx, cy in q:                                   # the tiles reachable from here (doors closed count)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            t = ((cx + dx) % S, (cy + dy) % S)
            if t not in seen and p.nav.cost(*t) is not None: seen.add(t); q.append(t)
    p.log('  L%d: %d tiles reachable from (%d,%d)' % (p.g.state['L'], len(seen), x, y))
    far = [t for t in seen if abs(t[0] - x) + abs(t[1] - y) > 12] or list(seen)
    for t in rnd.sample(far, min(n, len(far))):
        if p.g.state['L'] != p.nav.level: break
        try: p.walk(*t, max_steps=400); p.log('  explore -> %s: at (%d,%d)' % (t, p.g.state['X'], p.g.state['Y']))
        except Danger as e: p.log('  explore stopped: %s' % e); break
        except Exception as e: p.log('  explore -> %s: %s' % (t, e))
    return seen

def use_way(p, o):
    """step onto a cave / hole, or U)se a ladder from beside it; True if the level changed"""
    lv = p.g.state['L']
    try:
        if o['obj'] in (326, 308): p.walk(o['x'], o['y'])
        if p.g.state['L'] == lv and o['obj'] != 326:
            p.walk(o['x'], o['y'], adj=True); x, y = p.nav.pos()
            p.key('u'); p.key(VDIR[(o['x'] - x, o['y'] - y)], 'use %d' % o['obj'])
    except Exception as e: p.log('  way %d at (%d,%d): %s' % (o['obj'], o['x'], o['y'], e))
    return p.g.state['L'] != lv

def ways(p, up, reach=None):
    """the ladders / holes / caves within 24 tiles leading up (or down), nearest first"""
    objs, _ = p.g.objects(); x, y = p.nav.pos()
    if up: ws = [o for o in objs if (o['obj'] in (305, 106) and o['fr'] != 0) or o['obj'] == 326]
    else: ws = [o for o in objs if (o['obj'] in (305, 106) and o['fr'] == 0) or o['obj'] == 308]
    if reach is not None:
        ws = [o for o in ws if any(((o['x'] + dx) % p.nav.span, (o['y'] + dy) % p.nav.span) in reach
                                   for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)))]
    return sorted(ws, key=lambda o: abs(o['x'] - x) + abs(o['y'] - y))

def seg_cave(p, kw):
    """walk from Britain to the cave, step in (level 1), explore and fight, take a way down to level 2
    and explore it, then climb back to the surface"""
    p.nav.refresh(); p.walk(*CAVE)
    st = p.g.state
    if st['L'] != 1: p.issue('cave', 'stepping onto the cave at %s left us on L%d (%d,%d)' % (CAVE, st['L'], st['X'], st['Y'])); return
    p.log('  entered L1 at (%d,%d)' % (st['X'], st['Y'])); arrived = {1: (st['X'], st['Y'])}
    st = p.use_pack(90)                                 # light a torch: the dungeon is dark
    if st is None or 'lit' not in st['msg'].lower(): p.issue('cave', 'lighting a torch: %r' % (st and st['msg']))
    reach = explore(p)
    for o in ways(p, up=False, reach=reach):
        if use_way(p, o):
            st = p.g.state; p.log('  L%d at (%d,%d) by %d at (%d,%d)' % (st['L'], st['X'], st['Y'], o['obj'], o['x'], o['y']))
            arrived[st['L']] = (st['X'], st['Y'])
            if st['L'] != 2: p.issue('cave', 'a way down from L1 led to L%d' % st['L'])
            explore(p, seed=2); break
    for _ in range(4):                                  # climb: each level's nearest way up
        if p.g.state['L'] == 0: break
        p.nav.refresh(); lv = p.g.state['L']
        if lv in arrived:                               # back to where we came in: the way up is there
            try: p.walk(*arrived[lv], near=2)
            except Exception as e: p.log('  back to the L%d arrival %s: %s' % (lv, arrived[lv], e))
        for o in ways(p, up=True):
            if use_way(p, o):
                if p.g.state['L'] != lv - 1: p.issue('cave', 'a way up from L%d led to L%d' % (lv, p.g.state['L']))
                break
        else: break
    st = p.g.state
    if st['L'] != 0: p.issue('cave', 'could not get back to the surface from L%d (%d,%d)' % (st['L'], st['X'], st['Y']))
    else: p.log('  back on the surface at (%d,%d)' % (st['X'], st['Y']))

ORB = {3: (435, 395), 8: (307, 352), 19: (923, 876)}     # moongates.md §3: position -> destination (surface)

def orb_trip(p, pos, shot=False):
    """U)se the orb, walk its crosshair to the box tile of position pos, Enter, step into the red gate;
    True if the party arrived at that position's destination"""
    dx, dy = pos % 5 - 3, pos // 5 - 2
    x, y = p.nav.pos(); p.nav.refresh()
    if p.nav.cost((x + dx) % p.nav.span, (y + dy) % p.nav.span) is None:     # the gate's tile must be open:
        for cx, cy in [(x + a, y + b) for a in range(-6, 7) for b in range(-6, 7)]:   # find a spot where it is
            if p.nav.cost(cx % p.nav.span, cy % p.nav.span) is not None and p.nav.cost((cx + dx) % p.nav.span, (cy + dy) % p.nav.span) is not None:
                try: p.walk(cx, cy); break
                except Exception: continue
        x, y = p.nav.pos()
    st = p.use_pack(87)
    if st is None or st['verb'] != 'O': p.issue('orb', 'U)se the orb: verb=%r msg=%r' % (st and st['verb'], st and st['msg'])); return False
    for _ in range(abs(dx)): p.key('right' if dx > 0 else 'left')
    for _ in range(abs(dy)): p.key('down' if dy > 0 else 'up')
    if shot: p.g.shot('orb_crosshair.png')
    st = p.key('ret', 'orb gate %d' % pos)
    if 'gate' not in st['msg'].lower(): p.issue('orb', 'position %d (%+d,%+d): %r' % (pos, dx, dy, st['msg'])); return False
    tx, ty = ORB[pos]
    for _ in range(3):                                  # walk into the gate (the party follows over a few turns)
        try: p.nav.goto(x + dx, y + dy, on_step=lambda s: (s['X'], s['Y']) == (tx, ty))
        except Exception as e: p.log('  into the gate: %s' % e)
        for _ in range(8):
            if (p.g.state['X'], p.g.state['Y']) == (tx, ty) or p.g.state['busy'] == 0: break
            p.key('space')
        if (p.g.state['X'], p.g.state['Y']) == (tx, ty): break
    st = p.g.state
    if (st['L'], st['X'], st['Y']) != (0, tx, ty):
        p.issue('orb', 'position %d: arrived at L%d (%d,%d), expected (%d,%d)' % (pos, st['L'], st['X'], st['Y'], tx, ty)); return False
    p.log('  orb position %d -> (%d,%d)' % (pos, tx, ty)); return True

def seg_orb(p, kw):
    """the Orb of the Moons: to position 3, then 19, then home to the throne room (8)"""
    for pos in (3, 19, 8):
        if not orb_trip(p, pos, shot=(pos == 3)): break

SHRINE = dict(pos=4, field=(503, 359), rune=243, mantra='mu', wrong='om')   # Compassion (usecode_use.md runes, moongates.md §3)

def use_rune(p, rune, word):
    """U)se the rune from the pack and type a mantra; the message after Enter"""
    st = p.use_pack(rune)
    if st is None: p.issue('shrine', 'rune %d not in the pack' % rune); return None
    if not st['mantra']: p.issue('shrine', 'U)se rune %d: no Mantra: prompt (msg %r)' % (rune, st['msg'])); p.idle(); return None
    return p.g.type(word)['msg']

def talk_object(p, x, y):
    """T)alk with the crosshair on (x,y) (a shrine or statue: its quality is the conversation)"""
    px, py = p.nav.pos(); dx, dy = x - px, y - py
    p.key('t')
    for _ in range(abs(dx)): p.key('right' if dx > 0 else 'left')
    for _ in range(abs(dy)): p.key('down' if dy > 0 else 'up')
    return p.key('ret', 'talk to (%d,%d)' % (x, y))

def seg_shrine(p, kw):
    """the Compassion quest: Anya's permission, Ariana's rune; the orb to the shrine, its guardians,
    a wrong and the right mantra on the rune at the force field, then meditate at the shrine"""
    sh = SHRINE; fx, fy = sh['field']
    p.talk_to(13, ['rune'], 'Ariana', yes=True)                      # "Do you need it?" y, y: ask my parents
    p.talk_to(15, ['rune'], 'Anya')                                  # her permission to borrow it
    p.talk_to(13, ['rune'], 'Ariana', yes=True)                      # "Did you get permission?" y
    if not any(r['obj'] == sh['rune'] for r in p.g.objects()[1]):
        p.issue('shrine', 'Ariana did not give the rune of Compassion'); return
    m = use_rune(p, sh['rune'], sh['mantra'])                        # away from the field: nothing
    if m is not None and 'no effect' not in m.lower(): p.issue('shrine', 'the right mantra away from the shrine: %r' % m)
    ORB[sh['pos']] = (fx, fy - 1)
    if not orb_trip(p, sh['pos']): return
    if p.foes(): p.fight(); p.heal_up()                             # the guardians hatch at the shrine
    def field(): return [o for o in p.g.objects()[0] if o['obj'] == 51 and (o['x'], o['y']) == (fx, fy)]
    if not field(): p.issue('shrine', 'no force field at the shrine before the mantra')
    p.walk(fx, fy, near=1)
    m = use_rune(p, sh['rune'], sh['wrong'])
    if m is not None and 'wrong' not in m.lower(): p.issue('shrine', 'a wrong mantra said %r' % m)
    if not field(): p.issue('shrine', 'a wrong mantra removed the force field')
    m = use_rune(p, sh['rune'], sh['mantra'])
    if m is not None and 'done' not in m.lower(): p.issue('shrine', 'the right mantra said %r' % m)
    if field(): p.issue('shrine', 'the right mantra left the force field'); return
    lv0 = {m_['id']: m_['lvl'] for m_ in p.g.state['party']}
    st = talk_object(p, fx, fy); out = [('', st['conv'])]
    if not st['talk']: p.issue('shrine', 'T)alk to the shrine: talk=%s msg=%r' % (st['talk'], st['msg'])); return
    for _ in range(8):                                               # meditate? y / who? 1 (the Avatar) / the mantra
        st = p.g.state
        if not st['ask']: break
        a = 'y' if st.get('askc') else ('1' if st.get('num') else sh['mantra'])
        st = p.say(a); p.check('shrine %r' % a); out.append((a, st['conv']))
    p.idle(); p.transcripts.setdefault('shrine%d' % sh['pos'], []).append(out)
    text = ' '.join(r for _, r in out).lower()
    if (sh['mantra'] + '...') not in text: p.issue('shrine', 'the shrine did not take the mantra: %r' % text[-300:])
    lv1 = {m_['id']: m_['lvl'] for m_ in p.g.state['party']}
    p.log('  shrine: levels %s -> %s' % (lv0, lv1))
    if 'gained a level' in text and lv1.get(1) != lv0.get(1) + 1: p.issue('shrine', 'the shrine said a level was gained; Avatar %s -> %s' % (lv0.get(1), lv1.get(1)))

SEGS = dict(castle=seg_castle, bridge=seg_bridge, britain=seg_britain, shop=seg_shop, healer=seg_healer, cave=seg_cave, orb=seg_orb, shrine=seg_shrine)

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
            p.heal_up()
            try: SEGS[s](p, kw)
            except Danger as e: p.issue('danger', '%s: %s' % (s, e))
            except Exception as e: p.issue('crash', '%s: %s: %s' % (s, type(e).__name__, e)); raise
            st = g.state
            log('== %s done in %.0fs: L%d (%d,%d) %02d:%02d day %d keys %d issues %d' % (s, time.time() - ts, st['L'], st['X'],
                st['Y'], st['min'] // 60, st['min'] % 60, st['day'], st['k'], len(p.issues)))
    finally:
        json.dump({str(k): v for k, v in p.transcripts.items()}, open(os.path.join(out, 'transcripts.json'), 'w'), indent=1)
        g.shot('final.png'); g.quit()
        log('run %.0fs, %d issues' % (time.time() - t0, len(p.issues)))

if __name__ == '__main__': main()
