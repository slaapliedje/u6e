#!/usr/bin/env python3
"""run.py <drive folder> <out dir> [segment ...]: the bot's walkthrough, segment by segment.
Each segment plays part of the game with the real keyboard; issues.jsonl and transcripts.json
collect what looked wrong. Segments: castle, bridge, britain, shop, sell, death, healer, help, cave, orb, shrine (default: all, in order)."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from u6bot import Game
from play import Player, keywords, Danger
from nav import VDIR, DIRV, NavError

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
    """Tiberius (26) heals in his shop 9-18 (worktype 0x90). Carrying a companion's body, he asks 400
    gold at once: if the carrier holds 400 the companion stands up (his script then takes nothing,
    as in the reference; only a party collection pays); short, he says how short and the body stays.
    Carrying none, Resurrect says nobody is dead. Heal lists the wounded (the dead too) and mends
    the chosen living one to full for 30 of the patient's own gold (with less: free at karma 40 or
    more, else refused; a companion holds no gold); all well when nobody is hurt"""
    if not wait_for_work(p, 26, 'Tiberius'): return
    party = p.g.state['party']; g0 = p.g.state['gold']
    dead = [m['id'] for m in party if m['hp'] == 0]
    q = getattr(p, 'expect_body', None)
    if q: p.approach(26); party = p.g.state['party']; g0 = p.g.state['gold']; dead = [m['id'] for m in party if m['hp'] == 0]
    if q and not [r for r in p.g.objects()[1] if r['obj'] == 339 and r['q'] == q]:   # lost on the way (the Avatar fell, all were recalled)
        p.issue('healer', 'the body of %d was gone before Tiberius (party %s, msg %r)' % (q, party, p.g.state['msg'][:80])); return
    hurt = [m['id'] for m in party if m['hp'] < m['max']]                # the numbered "Which of you?" list
    alive_hurt = [i for i in hurt if i not in dead]
    body = [r['q'] for r in p.g.objects()[1] if r['obj'] == 339]
    pick = hurt.index(alive_hurt[0]) + 1 if alive_hurt else 1
    out = p.talk_to(26, (['y'] if body else ['resu']) + ['heal', str(pick), 'y'], 'Tiberius', yes=True)
    if out is None: p.issue('healer', 'could not reach Tiberius'); return
    text = ' '.join(r for _, r in out); low = text.lower()
    party = p.g.state['party']; g1 = p.g.state['gold']
    hp = {m['id']: m['hp'] for m in party}; mx = {m['id']: m['max'] for m in party}
    body1 = [r['q'] for r in p.g.objects()[1] if r['obj'] == 339]
    p.log('  Tiberius: dead %s, hurt %s, body %s -> hp %s, body %s, gold %d -> %d' % (dead, hurt, body, hp, body1, g0, g1))
    spent = 0
    if body and dead:
        q = body[0]
        if g0 >= 400:                                    # the script tests the carrier's own gold and then raises
            if hp.get(q, 0) == 0 or q in body1: p.issue('healer', 'offered 400 for %d: hp %s, body %s' % (q, hp.get(q), body1))   # without taking it (usecode_vm.md §6f)
            elif not p.actor(q): p.issue('healer', 'resurrected %d is not on the map' % q)
        else:
            if 'short' not in low: p.issue('healer', 'resurrect with %d gold: no "short" (%r)' % (g0, out[:2]))
            if hp.get(q, 0) != 0 or q not in body1: p.issue('healer', 'resurrected %d without paying (hp %s, body %s)' % (q, hp.get(q), body1))
    elif not dead and 'none of thy party is dead' not in low: p.issue('healer', 'resurrect with nobody dead: %r' % out[1:2])
    if not alive_hurt:
        if 'art well' not in text and 'are well' not in text and not dead: p.issue('healer', 'nobody hurt, no "well": %r' % out[-3:])
    else:
        h = alive_hurt[0]; g_before = g0 - spent
        own = g_before if h == 1 else 0                  # the script charges the PATIENT's gold; a companion holds none (pooled pack, §6f)
        if hp[h] == mx[h]:
            if g_before - g1 != (30 if own >= 30 else 0): p.issue('healer', 'heal of %d cost %d with %d gold' % (h, g_before - g1, g_before))
            if own < 30 and 'without payment' not in low: p.issue('healer', 'free heal of %d without "without payment" (%r)' % (h, out[-2:]))
        elif not (own < 30 and 'without an offering' in low): p.issue('healer', 'heal left %d at %d/%d (%r)' % (h, hp[h], mx[h], out[-3:]))
    for d in dead:
        if d not in alive_hurt and hp.get(d, 0) and not (body and g0 >= 400): p.issue('healer', 'dead %d rose without a resurrection' % d)

def member(p, aid):
    return next((m for m in p.g.state['party'] if m['id'] == aid), None)

def strike(p, aid, name, waits=8):
    """A)ttack party member aid once it stands beside the Avatar (walking up to it); the state after"""
    for _ in range(waits):
        a = p.actor(aid)
        if not a: return None
        x, y = p.nav.pos(); dx, dy = a['x'] - x, a['y'] - y
        if max(abs(dx), abs(dy)) == 1: break
        p.nav.refresh()                                                 # followers stand still while the Avatar waits: walk up
        try: p.nav.goto(a['x'], a['y'], adj=True, max_steps=30)
        except NavError as e: p.log('  to %s: %s' % (name, e)); p.key('space', 'wait')
    else: return None
    p.key('a', 'attack')
    if dx: p.key('right' if dx > 0 else 'left')
    if dy: p.key('down' if dy > 0 else 'up')
    return p.key('ret', 'strike %s' % name)

def seg_death(p, kw):
    """the Avatar strikes Shamino (3) until he is hurt, then Iolo (4) until he dies (combat.md §3d):
    a struck companion never turns hostile; at 0 HP "Iolo killed!", his body (339, quality 4) lies
    where he stood, he leaves the map but stays in the roster at 0 HP, and karma drops 5; then the
    Avatar picks the body up (the healer segment takes it to Tiberius)"""
    k0 = p.g.state['karma']; p.meant.add(4)
    for _ in range(12):
        st = strike(p, 3, 'Shamino')
        if st is None: p.issue('death', 'Shamino never came beside the Avatar'); return
        if member(p, 3)['hp'] < member(p, 3)['max']: break
    m3, a3 = member(p, 3), p.actor(3)
    p.log('  Shamino struck: %r, hp %d/%d' % (st['msg'], m3['hp'], m3['max']))
    if m3['hp'] == m3['max']: p.issue('death', 'Shamino never hurt in 12 blows (%r)' % st['msg'])
    if a3 and a3['hostile']: p.issue('death', 'Shamino turned hostile when struck')
    for _ in range(60):
        a = p.actor(4)
        if not a: break
        spot = (a['x'], a['y'])
        st = strike(p, 4, 'Iolo')
        if st is None: p.issue('death', 'Iolo never came beside the Avatar'); return
        if p.actor(4) and p.actor(4)['hostile']: p.issue('death', 'Iolo turned hostile when struck'); return
        if 'Iolo killed' in st['msg']: break
    else: p.issue('death', 'Iolo alive after 60 blows (%r)' % st['msg']); return
    p.log('  Iolo: %r at %s, karma %d -> %d' % (st['msg'], spot, k0, p.g.state['karma']))
    if 'Iolo killed' not in st['msg']: p.issue('death', 'no "Iolo killed!" (%r)' % st['msg'])
    if p.actor(4): p.issue('death', 'dead Iolo still on the map: %r' % p.actor(4))
    if not member(p, 4) or member(p, 4)['hp'] != 0: p.issue('death', 'roster after the death: %r' % p.g.state['party'])
    if p.g.state['karma'] != max(k0 - 5, 0): p.issue('death', 'karma %d -> %d' % (k0, p.g.state['karma']))
    body = [o for o in p.g.objects()[0] if o['obj'] == 339 and o['q'] == 4]
    if not body: p.issue('death', 'no body of Iolo near %s' % (spot,)); return
    b = body[0]
    if (b['x'], b['y']) != spot: p.log('  the body lies at (%d,%d), not %s' % (b['x'], b['y'], spot))
    p.nav.refresh(); x, y = p.nav.pos()
    if abs(b['x'] - x) + abs(b['y'] - y) != 1: p.nav.goto(b['x'], b['y'], adj=True); x, y = p.nav.pos()
    p.key('g'); st = p.key(VDIR[(b['x'] - x, b['y'] - y)], 'get the body')
    held = [r for r in p.g.objects()[1] if r['obj'] == 339 and r['q'] == 4]
    p.log('  get: %r, carried %d' % (st['msg'], len(held)))
    if not held: p.issue('death', 'could not pick up the body: %r' % st['msg'])

def seg_help(p, kw):
    """C)ast Help with a companion dead (magic_skills.md §5d): Lord British's revival. The party must
    stand in the throne room (307,352) on the surface, the dead up on the map, everyone at full HP,
    the hour one when he sits on his throne, karma unchanged"""
    dead = [m['id'] for m in p.g.state['party'] if m['hp'] == 0]
    k0 = p.g.state['karma']
    st = p.key('c', 'cast')
    if not st['cast']: p.issue('help', 'C did not open the spell prompt'); p.idle(); return
    st = p.g.type('help'); p.check('cast help')
    for _ in range(40):
        if not p.g.state['busy']: break
        p.key('space', 'recall')
    st = p.g.state; p.nav.refresh(); hour = st['min'] // 60
    p.log('  Help: %r, L%d (%d,%d) %02d:%02d, dead %s -> party %s, karma %d -> %d' % (st['msg'][:80], st['L'], st['X'], st['Y'], hour, st['min'] % 60, dead,
          ' '.join('%d:%d/%d' % (m['id'], m['hp'], m['max']) for m in st['party']), k0, st['karma']))
    if (st['L'], st['X'], st['Y']) != (0, 307, 352): p.issue('help', 'Help landed at L%d (%d,%d)' % (st['L'], st['X'], st['Y']))
    if any(m['hp'] != m['max'] for m in st['party']): p.issue('help', 'not all healed: %r' % st['party'])
    for d in dead:
        if not p.actor(d): p.issue('help', 'dead %d not back on the map' % d)
    if not (8 <= hour < 12 or 14 <= hour < 18): p.issue('help', 'hour %d: Lord British is not on his throne' % hour)
    if st['karma'] != k0: p.issue('help', 'karma %d -> %d' % (k0, st['karma']))
    left = [r['q'] for r in p.g.objects()[1] if r['obj'] == 339 and r['q'] in dead]
    if left: p.issue('help', 'the bodies of %s still in the pack after they rose' % left)
    p.meant.clear()

def wait_for_work(p, aid, name, limit=400):
    """pass turns (space) until actor aid is at work (worktype 0x90); False if it never is"""
    for i in range(limit):
        a = p.actor(aid) if i % 10 == 0 else a
        if a and a.get('wt') == 0x90: return True
        p.key('space', 'wait for %s' % name)
    p.log('  %s never came to work (wt=%s, %02d:%02d)' % (name, a and a.get('wt'), p.g.state['min'] // 60, p.g.state['min'] % 60))
    return False

def seg_sell(p, kw):
    """sell Max (18) a weapon: he names one the party carries and a price; yes: the item leaves the
    pack and the gold goes up by the price"""
    import re
    if not wait_for_work(p, 18, 'Max'): p.issue('sell', 'Max never at work'); return
    def carried():                                                      # the pack and everything readied, by type
        c = {}
        for r in p.g.objects()[1]: c[r['obj']] = c.get(r['obj'], 0) + (r['qty'] or 1)
        for r in p.g.ready: c[r['obj']] = c.get(r['obj'], 0) + 1
        return c
    g0 = p.g.state['gold']; pack0 = carried()
    out = p.talk_to(18, ['sell', '1', 'y'], 'Max')         # which of you: 1 (the Avatar holds the pooled pack, usecode_vm.md §6f)
    if out is None: p.issue('sell', 'could not reach Max'); return
    text = ' '.join(r for _, r in out)
    m = re.search(r'take (\d+) gold for that ([^,?]+)', text)
    pack1 = carried()
    gone = {o: pack0[o] - pack1.get(o, 0) for o in pack0 if pack0[o] > pack1.get(o, 0)}
    g1 = p.g.state['gold']
    p.log('  Max: offer %s, gold %d -> %d, pack lost %s' % (m and m.groups(), g0, g1, gone))
    if not m: p.issue('sell', 'no offer from Max: %r' % out[-3:]); return
    if re.search(r'Aye,\s+has', text): p.issue('sell', 'no name in the offer: %r' % text[:120])
    if g1 - g0 != int(m.group(1)) or sum(gone.values()) != 1: p.issue('sell', 'sold %s for %s: gold %d -> %d, pack lost %s' % (m.group(2), m.group(1), g0, g1, gone))

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
    if st is not None and 'burned out' in st['msg'].lower(): st = p.use_pack(90)   # the lit one went out as we stepped in
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

def orb_trip(p, pos, shot=False, quiet=False):
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
    if 'gate' not in st['msg'].lower():
        if quiet and 'Failed' in st['msg']: p.log('  orb position %d: %r, moving' % (pos, st['msg']))
        else: p.issue('orb', 'position %d (%+d,%+d): %r' % (pos, dx, dy, st['msg']))
        return False
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
    """the Orb of the Moons: to position 3, then 19, then home to the throne room (8); first a camp at
    the cave mouth if anyone is hurt (the shrine's guardians come next)"""
    p.rest_if_hurt(0.9)
    for pos in (3, 19, 8):
        for attempt in range(3):                                         # someone standing on the gate's tile: "Failed."
            if orb_trip(p, pos, shot=(pos == 3 and attempt == 0), quiet=attempt < 2): break
            if 'Failed' not in p.g.state['msg']: return
            p.idle(); p.nav.refresh(); x, y = p.nav.pos()
            for dx, dy in DIRV.values():
                if p.nav.cost(x + 2 * dx, y + 2 * dy) is not None:
                    try: p.nav.goto(x + 2 * dx, y + 2 * dy); break
                    except NavError: continue
        else: return

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
    for _ in range(900):                                             # Anya and Ariana talk by day: wait for morning
        if 9 <= p.g.state['min'] // 60 < 18: break
        if p.g.state['foes'] and p.foes(): p.fight(); p.heal_up(); continue
        p.key('space', 'wait for day')
    p.log('  shrine quest from %02d:%02d; Ariana %s' % (p.g.state['min'] // 60, p.g.state['min'] % 60, p.actor(13)))
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

def norm(name):
    """a merchant's item name and a look name compared alike: case, armour/armor, a leading article"""
    n = name.lower().replace('armour', 'armor').strip()
    for a in ('the ', 'a ', 'an '):
        if n.startswith(a): n = n[len(a):]
    return n

def seg_trade(p, kw):
    """sell Max (18) the loot the fights brought (eggs.md §3c), never what the party has readied: one
    item a talk, each sale paying its price, until the purse holds 400 gold or he wants nothing more"""
    import re
    if not wait_for_work(p, 18, 'Max', limit=2500): p.issue('trade', 'Max never at work'); return
    g0 = p.g.state['gold']; sold = []; closed = 0
    for _ in range(30):
        if p.g.state['gold'] >= 400: break
        _, inv = p.g.objects()
        loose = {norm(r['name']) for r in inv}                        # the pack's own things (readied ones are not in it)
        def pick(conv):
            items = re.findall(r'(\d+)\) ([A-Za-z\' -]+?)(?= \d+\)| "|$)', conv.split('> sell', 1)[-1])
            for num, name in items:
                if norm(name) in loose: return num
            return None
        gb = p.g.state['gold']
        out = p.talk_to(18, ['sell', pick, 'y'], 'Max')
        if out is None: p.issue('trade', 'could not reach Max'); return
        text = ' '.join(r for _, r in out)
        m = re.search(r'take (\d+) gold for that ([^,?]+)', text)
        if not m and 'open' in text and closed < 3:                   # the shop shut while we walked over: next opening
            closed += 1
            for _ in range(2500):                                     # his shop hours (6-12, 14-17)
                h = p.g.state['min'] // 60
                if 6 <= h < 11 or 14 <= h < 16: break
                p.key('space', 'shop shut')
            if not wait_for_work(p, 18, 'Max', limit=2500): break
            continue
        if not m: break
        price = int(m.group(1)); sold.append('%s %d' % (m.group(2), price))
        if p.g.state['gold'] != gb + price: p.issue('trade', 'sold %s for %d: gold %d -> %d' % (m.group(2), price, gb, p.g.state['gold']))
    p.log('  sold %d to Max: %s; gold %d -> %d' % (len(sold), ', '.join(sold)[:500], g0, p.g.state['gold']))

def seg_farm(p, kw):
    """more cave trips while the purse and Max's share of the loot fall short of 450 gold (the 400 of
    a resurrection, Tiberius's cures at 10 a member, a margin; at most 5 trips)"""
    for trip in range(5):
        _, inv = p.g.objects()
        worth = sum({'iron helm': 10, 'leather armor': 20, 'chain mail': 40, 'plate mail': 80, 'sword': 25}.get(norm(r['name']), 0) * (r['qty'] or 1) for r in inv)
        heals = min(sum(r['qty'] or 1 for r in inv if r['obj'] == o) for o in (68, 72))   # ginseng, spider silk (Heal)
        p.log('  farm: gold %d, loot for Max ~%d, heals left %d' % (p.g.state['gold'], worth, heals))
        if p.g.state['gold'] + worth >= 450: break
        if p.g.state['L'] != 0: p.log('  farm: stuck below (L%d), no more trips' % p.g.state['L']); break
        if heals < 3: p.log('  farm: too few reagents to heal through another trip'); break
        if p.cure_up() and not tiberius_cure(p):                        # no red potion: Tiberius cures for 10 gold
            p.log('  farm: %s poisoned and no cure: no more trips' % p.poisoned()); break
        if p.g.state['L'] != 0: break
        if (p.g.state['X'], p.g.state['Y']) == CAVE:                   # standing on the cave: step off (so walking back on enters it)
            p.nav.refresh()
            for dx, dy in DIRV.values():
                if p.nav.cost(CAVE[0] + dx, CAVE[1] + dy) is not None:
                    try: p.nav.goto(CAVE[0] + dx, CAVE[1] + dy); break
                    except NavError: continue
        p.rest_if_hurt(0.95)                                           # a camp at the cave mouth heals for free
        seg_cave(p, kw)

def tiberius_cure(p):
    """Tiberius (26) cures the poisoned: "cure" lists them by number and asks 10 gold for the one
    chosen (with less he cures for nothing); once a member. True when nobody is left poisoned"""
    sick = p.poisoned()
    if not sick: return True
    if not wait_for_work(p, 26, 'Tiberius'): return False
    g0 = p.g.state['gold']
    out = p.talk_to(26, ['cure', '1', 'y'] * len(sick), 'Tiberius', yes=True)
    if out is None: p.issue('cure', 'could not reach Tiberius'); return False
    left, g1 = p.poisoned(), p.g.state['gold']
    p.log('  Tiberius cured %s -> still poisoned %s, gold %d -> %d' % (sick, left, g0, g1))
    if left: p.issue('cure', 'Tiberius left %s poisoned (%r)' % (left, out[-3:]))
    want = sum(10 if g0 - 10 * k >= 10 else 0 for k in range(len(sick) - len(left)))
    if g0 - g1 != want: p.issue('cure', 'cures cost %d, expected %d' % (g0 - g1, want))
    return not left

def seg_paid(p, kw):
    """with 400 gold: Tiberius first cures the poisoned (poison wears the party down on the way, and
    it never lifts by itself), then a companion dies again (the death segment) and Tiberius raises
    him once the offering of 400 is accepted (the healer segment checks the body, the purse and the
    risen companion)"""
    p.cure_up(); need = 400 + 10 * len(p.poisoned())
    if p.g.state['gold'] < need: p.log('  only %d gold of %d: no paid resurrection' % (p.g.state['gold'], need)); return
    if not tiberius_cure(p): return
    g0 = p.g.state['gold']
    seg_death(p, kw)
    if member(p, 4)['hp'] != 0: return
    p.expect_body = 4
    try: seg_healer(p, kw)
    finally: p.expect_body = None
    m4 = member(p, 4)
    p.log('  paid resurrection: gold %d -> %d, Iolo %d/%d, on the map %s' % (g0, p.g.state['gold'], m4['hp'], m4['max'], bool(p.actor(4))))
    if m4['hp'] == 0: p.issue('paid', 'Iolo still dead after paying')
    p.meant.clear()

SEGS = dict(castle=seg_castle, bridge=seg_bridge, britain=seg_britain, shop=seg_shop, sell=seg_sell, death=seg_death, healer=seg_healer, help=seg_help, cave=seg_cave, farm=seg_farm, trade=seg_trade, paid=seg_paid, orb=seg_orb, shrine=seg_shrine)

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
            p.heal_up(); p.cure_up()
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
