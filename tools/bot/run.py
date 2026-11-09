#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""run.py <drive folder> <out dir> [--from <checkpoint>] [segment | save:<checkpoint> ...]: the bot's walkthrough, segment by segment.
Each segment plays part of the game with the real keyboard; issues.jsonl and transcripts.json
collect what looked wrong. Segments: castle, bridge, britain, shop, sell, death, healer, help, cave, orb, shrine (default: all, in order)."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from u6bot import Game, kv
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
    if p.g.state['foes'] and p.foes(): p.fight()                        # the opening gargoyles keep up with a walking party (movement_turns.md §3b)
    p.nav.goto(303, 384, adj=True); x, y = p.nav.pos()
    st = p.act('u', VDIR[(303 - x, 384 - y)], 'crank')
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
    if g2 < 0 or g1 - g2 != 3 * (t2 - t1) or t2 <= t1: p.issue('shop', 'asking %d torches with %d gold: gold %d, torches +%d' % (many, g1, g2, t2 - t1))

def seg_healer(p, kw):
    """Tiberius (26) heals in his shop 9-18 (worktype 0x90). Carrying a companion's body, he asks 400
    gold at once: if the carrier holds 400 the companion stands up (his script then takes nothing,
    as in the reference; only a party collection pays); short, he says how short and the body stays.
    Carrying none, Resurrect says nobody is dead. Heal lists the wounded (the dead too) and mends
    the chosen living one to full for 30 of the patient's own gold (with less: free at karma 40 or
    more, else refused; a companion holds no gold); all well when nobody is hurt"""
    if not wait_for_work(p, 26, 'Tiberius'): return
    party = p.g.state['party']; g0 = p.g.state['gold']
    dead = missing(p)
    q = getattr(p, 'expect_body', None)
    if q: p.approach(26); party = p.g.state['party']; g0 = p.g.state['gold']; dead = missing(p)
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

def missing(p):
    """of the starting party, who is off the roster: a dead companion leaves it (combat.md §3d)"""
    return [a for a in (2, 3, 4) if not member(p, a)]

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
    return p.aim_fire(dx, dy, 'strike %s' % name)

def seg_death(p, kw):
    """the Avatar strikes Shamino (3) until he is hurt, then Iolo (4) until he dies (combat.md §3d):
    a struck companion never turns hostile; at 0 HP "Iolo killed!", his body (339, quality 4) lies
    where he stood, holding what he had; he leaves the map and the roster; karma stays; then the
    Avatar picks the body up (the healer segment takes it to Tiberius)"""
    if p.g.state['foes'] and p.foes(): p.fight()                        # an attack takes its time now: they'd answer
    k0 = p.g.state['karma']; p.meant.add(4)
    for _ in range(40):
        st = strike(p, 3, 'Shamino')
        if st is None: p.issue('death', 'Shamino never came beside the Avatar'); return
        if member(p, 3)['hp'] < member(p, 3)['max']: break
    m3, a3 = member(p, 3), p.actor(3)
    p.log('  Shamino struck: %r, hp %d/%d' % (st['msg'], m3['hp'], m3['max']))
    if m3['hp'] == m3['max']: p.issue('death', 'Shamino never hurt in 40 blows (%r)' % st['msg'])
    if a3 and a3['hostile']: p.issue('death', 'Shamino turned hostile when struck')
    for _ in range(200):
        a = p.actor(4)
        if not a: break
        spot = (a['x'], a['y'])
        st = strike(p, 4, 'Iolo')
        if st is None: p.issue('death', 'Iolo never came beside the Avatar'); return
        if p.actor(4) and p.actor(4)['hostile']: p.issue('death', 'Iolo turned hostile when struck'); return
        if 'Iolo killed' in st['msg']: break
    else: p.issue('death', 'Iolo alive after 200 blows (%r)' % st['msg']); return
    if not p.g.state['party'] or 'spot' not in locals(): p.issue('death', 'Iolo was gone before the blows'); return
    p.log('  Iolo: %r at %s, karma %d -> %d' % (st['msg'], spot, k0, p.g.state['karma']))
    if 'Iolo killed' not in st['msg']: p.issue('death', 'no "Iolo killed!" (%r)' % st['msg'])
    if p.actor(4): p.issue('death', 'dead Iolo still on the map: %r' % p.actor(4))
    if member(p, 4): p.issue('death', 'roster after the death: %r' % p.g.state['party'])
    if p.g.state['karma'] != k0: p.issue('death', 'karma %d -> %d' % (k0, p.g.state['karma']))
    body = [o for o in p.g.objects()[0] if o['obj'] == 339 and o['q'] == 4]
    if not body: p.issue('death', 'no body of Iolo near %s' % (spot,)); return
    b = body[0]
    if (b['x'], b['y']) != spot: p.log('  the body lies at (%d,%d), not %s' % (b['x'], b['y'], spot))
    p.nav.refresh(); x, y = p.nav.pos()
    if abs(b['x'] - x) + abs(b['y'] - y) != 1: p.nav.goto(b['x'], b['y'], adj=True); x, y = p.nav.pos()
    st = p.act('g', VDIR[(b['x'] - x, b['y'] - y)], 'get the body')
    held = [r for r in p.g.objects()[1] if r['obj'] == 339 and r['q'] == 4]
    p.log('  get: %r, carried %d' % (st['msg'], len(held)))
    if not held: p.log('  the body stays: %r' % st['msg']) if 'too heavy' in st['msg'] else p.issue('death', 'could not pick up the body: %r' % st['msg'])

def seg_help(p, kw):
    """C)ast Help with a companion dead (magic_skills.md §5d): Lord British's revival. The party must
    stand in the throne room (307,352) on the surface, the dead up on the map, everyone at full HP,
    the hour one when he sits on his throne, karma unchanged"""
    dead = missing(p)
    k0 = p.g.state['karma']
    st = p.key('c', 'cast')
    if not st['cast']: p.issue('help', 'C did not open the spell prompt'); p.idle(); return
    st = p.g.type('kl'); p.check('cast help')
    for _ in range(40):
        if not p.g.state['busy']: break
        p.key('space', 'recall')
    st = p.g.state; p.nav.refresh(); hour = st['min'] // 60
    p.log('  Help: %r, L%d (%d,%d) %02d:%02d, dead %s -> party %s, karma %d -> %d' % (st['msg'][:80], st['L'], st['X'], st['Y'], hour, st['min'] % 60, dead,
          ' '.join('%d:%d/%d' % (m['id'], m['hp'], m['max']) for m in st['party']), k0, st['karma']))
    if (st['L'], st['X'], st['Y']) != (0, 307, 352): p.issue('help', 'Help landed at L%d (%d,%d)' % (st['L'], st['X'], st['Y']))
    if not st['foes'] and any(m['hp'] != m['max'] for m in st['party']): p.issue('help', 'not all healed: %r' % st['party'])   # eggs hatch at the castle on arrival
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
    out = p.talk_to(18, ['sell', '1', '1', 'y'], 'Max')         # which of you: 1 (the Avatar holds the pooled pack, usecode_vm.md §6f)
    if out is None: p.issue('sell', 'could not reach Max'); return
    text = ' '.join(r for _, r in out)
    m = re.search(r'take (\d+) gold for that ([^,?]+)', text)
    pack1 = carried()
    gone = {o: pack0[o] - pack1.get(o, 0) for o in pack0 if pack0[o] > pack1.get(o, 0)}
    g1 = p.g.state['gold']
    p.log('  Max: offer %s, gold %d -> %d, pack lost %s' % (m and m.groups(), g0, g1, gone))
    if not m: p.issue('sell', 'no offer from Max: %r' % out[-3:]); return
    if re.search(r'Aye,\s+has', text): p.issue('sell', 'no name in the offer: %r' % text[:120])
    if g1 - g0 != int(m.group(1)) or sum(v for o, v in gone.items() if not 65 <= o <= 72) != 1: p.issue('sell', 'sold %s for %s: gold %d -> %d, pack lost %s' % (m.group(2), m.group(1), g0, g1, gone))

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
            p.act('u', VDIR[(o['x'] - x, o['y'] - y)], 'use %d' % o['obj'])
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

ORB = {1: (899, 499), 2: (935, 262), 3: (435, 395), 4: (503, 358), 5: (147, 883), 6: (919, 934), 8: (307, 352),
       10: (159, 942), 11: (739, 699), 15: (227, 131), 19: (923, 876), 20: (295, 38), 21: (75, 507), 22: (327, 822),
       16: (23, 22), 23: (387, 787), 24: (831, 166), 25: (667, 67)}   # moongates.md §3: position -> destination
ORB.update({7: (68, 45), 9: (188, 45), 17: (128, 86), 18: (108, 221)})   # the gargoyle world (moongates.md §3)
ORB_LEVEL = {16: 1, 7: 5, 9: 5, 17: 5, 18: 5}           # the destinations below the surface

def orb_trip(p, pos, shot=False, quiet=False):
    """U)se the orb, walk its crosshair to the box tile of position pos, Enter, step into the red gate;
    True if the party arrived at that position's destination"""
    # the box is 6 wide (dx -3..+2, dy -2..+2) and numbered (3+dx)+5(2+dy): some positions have two tiles
    offs = [(a, b) for b in range(-2, 3) for a in range(-3, 3) if (3 + a) + 5 * (2 + b) == pos]
    p.nav.refresh(); S = p.nav.span
    taken = {(a_['x'], a_['y']) for a_ in p.g.actors() if a_['id'] != 1}   # the Orb fails on a tile someone stands on
    def walkable_to(sx, sy):                            # an offset whose gate tile the party can walk to from (sx,sy)
        for a, b in offs:
            g_ = ((sx + a) % S, (sy + b) % S)
            if g_ in taken: continue
            if p.nav.cost(*g_) is not None and p.nav.route(sx, sy, {g_}, limit=4000) is not None: return a, b
        return None
    x, y = p.nav.pos(); o = walkable_to(x, y)
    if o is None and len(offs) == 1:                    # step toward the one gate tile: the followers fall in behind
        a, b = offs[0]; d = (0, -1) if b < 0 else (0, 1) if b > 0 else ((1, 0) if a > 0 else (-1, 0))
        for _ in range(3):
            if p.nav.cost((x + d[0]) % S, (y + d[1]) % S) is None: break
            p.key(VDIR[d], 'make room'); x, y = p.nav.pos()
        taken = {(a_['x'], a_['y']) for a_ in p.g.actors() if a_['id'] != 1}; o = walkable_to(x, y)
    if o is None:                                       # stand where a gate can be reached
        for cx, cy in sorted([(x + a, y + b) for a in range(-6, 7) for b in range(-6, 7)], key=lambda t: abs(t[0] - x) + abs(t[1] - y)):
            if p.nav.cost(cx % S, cy % S) is None or walkable_to(cx % S, cy % S) is None: continue
            try: p.walk(cx % S, cy % S)
            except Exception: continue
            x, y = p.nav.pos(); o = walkable_to(x, y)
            if o: break
    dx, dy = o if o else offs[0]
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
        except Exception as e:
            p.log('  into the gate: %s' % e)
            if p.foes(): p.fight()                      # someone stands in the gate: fight it off, then walk in
        for _ in range(8):
            if (p.g.state['X'], p.g.state['Y']) == (tx, ty) or p.g.state['busy'] == 0: break
            p.key('space')
        if (p.g.state['X'], p.g.state['Y']) == (tx, ty): break
    st = p.g.state
    if (st['L'], st['X'], st['Y']) != (ORB_LEVEL.get(pos, 0), tx, ty):
        p.issue('orb', 'position %d: arrived at L%d (%d,%d), expected (%d,%d)' % (pos, st['L'], st['X'], st['Y'], tx, ty)); return False
    p.log('  orb position %d -> (%d,%d)' % (pos, tx, ty)); return True

def orb_go(p, pos):
    """the Orb to position pos, stepping aside when someone stands on the gate's tile; True if there"""
    tx, ty = ORB[pos]; st = p.g.state
    if st['L'] == ORB_LEVEL.get(pos, 0) and abs(st['X'] - tx) <= 12 and abs(st['Y'] - ty) <= 12: return True   # already there
    for attempt in range(5):
        if orb_trip(p, pos, quiet=attempt < 4): p.nav.refresh(); return True   # a new place: its doors weren't in the old map
        if 'Failed' not in p.g.state['msg']: break
        p.idle(); p.nav.refresh()
        spot = p.open_spot(6 + 4 * attempt)                               # somewhere roomier, farther each time (out of a shop)
        if spot:
            try: p.nav.goto(*spot, max_steps=400)
            except NavError: pass
    p.issue('orb', 'no Orb trip to position %d from L%d (%d,%d): %r' % (pos, p.g.state['L'], p.g.state['X'], p.g.state['Y'], p.g.state['msg'][-60:]))
    return False

def carried(p, obj_n):
    return any(r['obj'] == obj_n for r in p.g.objects()[1])

HEAVY = (73, 90, 394, 396, 62, 48, 149, 422, 421, 284, 264, 141, 140)    # moonstones, torches, lenses, cube, glass sword, deed, balloon parts, pole, decorations

def lighten(p):
    """the Avatar carries at most twice his strength (G)et says 'The total is too heavy.'): his heavy quest
    things go to the strongest-backed companion, Dupre, else the next"""
    for _ in range(4): p.key('space', 'the party gathers')               # the crosshair finds only who is in view (dark, strung out: "To nobody.")
    ids = [m['id'] for m in p.g.state['party'] if m['id'] != 1 and m['hp'] > 0 and m['id'] != 9 and p.in_view(m['id'])]
    if not ids: return
    k = 0
    for obj in HEAVY:                                                    # spread over the companions, Dupre first
        if count(p, obj, 1): give_to(p, obj, ids[k % len(ids)]); k += 1

def get_at(p, x, y, obj_n, tries=3):
    """walk beside (x,y) and G)et until obj_n is in the pack (what lies on top comes first); True if got"""
    for _ in range(tries):
        p.walk(x, y, adj=True); px, py = p.nav.pos()
        st = p.act('g', VDIR[(x - px, y - py)], 'get at (%d,%d)' % (x, y))
        p.log('  get at (%d,%d): %r' % (x, y, st['msg'][-120:]))
        if carried(p, obj_n): return True
        if 'too heavy' in st['msg']:
            p.log('  loads: %s' % ' '.join('%d:%s/%s' % (m['id'], m.get('wt'), m.get('lim')) for m in p.g.state['party'])); lighten(p)
    p.issue('get', 'no obj %d from (%d,%d): %r' % (obj_n, x, y, p.g.state['msg'][-200:])); return False

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

def move_off(p, x, y):
    """M)ove what lies on top at (x,y) to a neighbouring tile (taking it would be theft); the message"""
    p.walk(x, y, adj=True); px, py = p.nav.pos(); d = (x - px, y - py)
    for to in [d] + [v for v in DIRV.values() if v != d and v != (-d[0], -d[1])]:
        p.act('m', VDIR[d], 'move at (%d,%d)' % (x, y)); st = p.key(VDIR[to], 'to')
        p.log('  move off (%d,%d) %s: %r' % (x, y, VDIR[to], st['msg'][-100:]))
        if 'Blocked' not in st['msg'] and 'not possible' not in st['msg'].lower(): return st['msg']
        p.idle()
    return p.g.state['msg']

def seg_justice(p, kw):
    """Yew: the rune of Justice lies under a potted plant in the Slaughtered Lamb (bot_walkthrough.md §3);
    the plant is moved aside, not taken"""
    if not orb_go(p, 15): return
    move_off(p, 237, 156)
    get_at(p, 237, 156, 245)
    if 'Stealing' in p.g.state['msg']: p.issue('justice', 'the rune counted as theft')

def seg_honor(p, kw):
    """Trinsic: the rune of Honor on its pedestal; Whitsaber first (he lets it be taken)"""
    if not orb_go(p, 23): return
    p.talk_to(76, ['rune'], 'Whitsaber')
    get_at(p, 411, 739, 247)

def use_at(p, x, y):
    """walk beside (x,y), U)se what is there; the state after"""
    p.walk(x, y, adj=True); px, py = p.nav.pos()
    return p.act('u', VDIR[(x - px, y - py)], 'use at (%d,%d)' % (x, y))

def got_rune(p, rune, who):
    if carried(p, rune): p.log('  rune %d: carried' % rune); return True
    p.issue('rune', '%s: no rune %d' % (who, rune)); return False

def seg_spirituality(p, kw):
    """Skara Brae: Marney's poem and basket, then her hope chest holds the rune of Spirituality"""
    if not orb_go(p, 21): return
    p.talk_to(89, ['marn', 'poem', 'bask'], 'Marney')
    st = use_at(p, 81, 493); p.log('  chest: %r' % st['msg'][-120:])
    get_at(p, 81, 493, 248, tries=5)

def seg_humility(p, kw):
    """New Magincia: Antonio asks for the humblest; Conor"""
    if not orb_go(p, 11): return
    p.talk_to(92, ['humb', 'y', 'conor'], 'Antonio', yes=True)
    got_rune(p, 249, 'Antonio')

def seg_sacrifice(p, kw):
    """the panpipes: a yew log from Ben (Yew), a board from Aaron (by Minoc), panpipes from Julia,
    the tune from Gwenno; Selganor's guild takes the player and gives the rune of Sacrifice"""
    if not orb_go(p, 15): return
    p.talk_to(54, ['log', 'y', '1', 'y'], 'Ben', yes=True)
    if not orb_go(p, 25): return
    p.talk_to(69, ['log', 'y'], 'Aaron', yes=True)
    p.talk_to(67, ['pipe', 'boar', 'y'], 'Julia', yes=True)
    p.talk_to(66, ['ston'], 'Gwenno')
    p.talk_to(64, ['rune', 'join', 'pipe', 'y', '6789878767653'], 'Selganor', yes=True)
    got_rune(p, 246, 'Selganor')

def give_to_avatar(p, obj_n, others=False):
    """M)ove a carried thing to the Avatar: M, M through the pack to it, Enter, then the crosshair on the Avatar
    (it starts on the leader) and Enter (usecode_use.md: "To " a party member). others: the first row someone
    else holds (the pack steps in row order)"""
    inv = p.g.objects()[1]
    rows = [k for k, r in enumerate(inv) if r['obj'] == obj_n and (not others or r.get('own', 1) != 1)]
    if not rows: return False
    name = inv[rows[0]].get('name', '')
    p.idle(); p.key('m', 'move')
    for k in range(len(inv) + 2):
        st = p.key('m', 'the pack')
        if st['msg'].startswith('Move the ' + name) and (not others or k >= rows[0]): break
    else: p.idle(); return False
    st = p.key('ret', 'this one'); st = p.key('ret', 'to the Avatar'); p.log('  %s to the Avatar: %r' % (name, st['msg'][-50:]))
    p.idle(); return True

def seg_valor(p, kw):
    """cheese from the castle kitchen for Sherry the mouse (9), who joins; Jhelom: the rune of Valor lies
    behind the mouse hole (213) at (156,844) in the Sword and Keg; Sherry alone through it"""
    if not orb_go(p, 8): return
    if not carried(p, 132): get_at(p, 320, 351, 132)
    if not carried(p, 63):                                             # lockpicks for Moonglow's tavern door: a chest by the Avatar's book
        st = use_at(p, 298, 367); p.log('  chest: %r' % st['msg'][-120:]); get_at(p, 298, 367, 63, tries=4)
    p.talk_to(9, ['chee', 'y', 'y', 'join', 'y'], 'Sherry', yes=True)
    ids = [m['id'] for m in p.g.state['party']]
    if 9 not in ids: p.issue('valor', 'Sherry did not join (party %s)' % ids); return
    if not orb_go(p, 5): return
    for _ in range(4):                                                 # the party two steps short, so Sherry can come by
        p.walk(156, 846)
        if p.nav.pos() == (156, 846): break
    st = p.key(str(ids.index(9) + 1), 'solo Sherry'); p.log('  solo: %r at (%d,%d)' % (st['msg'][-60:], st['X'], st['Y']))
    p.nav.refresh()
    for _ in range(4):
        p.walk(156, 844, adj=True); px, py = p.nav.pos(); d = (156 - px, 844 - py)
        if d in DIRV.values(): break
    else: p.issue('valor', 'Sherry could not stand beside the mouse hole: at (%d,%d)' % (px, py)); p.key('0'); return
    st = p.key(VDIR[d], 'into the hole')
    if (st['X'], st['Y']) != (156, 844): p.issue('valor', 'Sherry did not enter the hole: at (%d,%d) %r' % (st['X'], st['Y'], st['msg'][-80:]))
    st = p.key('up', 'into the nook'); p.log('  Sherry at (%d,%d)' % (st['X'], st['Y']))
    st = p.act('g', 'right', 'the rune'); p.log('  get: %r' % st['msg'][-80:])
    st = p.act('g', 'up', 'the ring'); p.log('  get: %r' % st['msg'][-80:])      # the invisibility ring beside it, for the shrines
    p.key('down'); p.key(VDIR[(-d[0], -d[1])])
    st = p.key('0', 'party mode'); p.log('  party: %r at (%d,%d)' % (st['msg'][-60:], st['X'], st['Y']))
    got_rune(p, 244, 'Sherry')
    give_to_avatar(p, 244); give_to_avatar(p, 258)                      # a mouse dies easily: the Avatar keeps them

_LADDERS = None
def ladders():
    """every ladder and hole in the new-game data: [(obj, frame, x, y, z)] (build/objfind; map_levels.md)"""
    global _LADDERS
    if _LADDERS is None:
        import subprocess
        exe = os.path.join(ROOT, 'build', 'objfind')
        if not os.path.exists(exe): subprocess.run(['make', '-C', ROOT, 'objfind'], check=True, capture_output=True)
        out = subprocess.run([exe, REF, '305', '106', '308'], check=True, capture_output=True, text=True).stdout
        _LADDERS = [tuple(int(v) for v in (f[0], f[1], f[4], f[5], f[6])) for f in (l.split() for l in out.splitlines()) if f[7] == '-1']
    return _LADDERS

def reach_set(p):
    """the tiles of this level the party can walk to from here (closed doors count as open)"""
    p.nav.refresh(); x, y = p.nav.pos(); S = p.nav.span
    seen, q = {(x, y)}, [(x, y)]
    for cx, cy in q:
        for dx, dy in DIRV.values():
            t = ((cx + dx) % S, (cy + dy) % S)
            if t not in seen and p.nav.cost(*t) is not None: seen.add(t); q.append(t)
    return seen

def beside(reach, x, y):
    return any(((x + dx), (y + dy)) in reach for dx, dy in [(0, 0)] + list(DIRV.values()))

def descend_to(p, tx, ty, tz, tried=None):
    """from a dungeon level, take ladders down until (tx,ty) on level tz can be walked to; ladders nearest
    the target first, climbing back to try the next when a level below leads nowhere. True if there"""
    tried = set() if tried is None else tried
    L = p.g.state['L']; reach = reach_set(p)
    if L == tz: return beside(reach, tx, ty)
    downs = [(o, fr, x, y) for o, fr, x, y, z in ladders() if z == L and ((o in (305, 106) and fr == 0) or o == 308)]
    downs = sorted((d for d in downs if beside(reach, d[2], d[3]) and (d[2], d[3], L) not in tried), key=lambda d: abs(d[2] - tx) + abs(d[3] - ty))
    for o, fr, x, y in downs[:6]:
        tried.add((x, y, L))
        p.log('  L%d: down at (%d,%d) toward (%d,%d,%d)' % (L, x, y, tx, ty, tz))
        try:
            if not use_way(p, {'obj': o, 'x': x, 'y': y}) or p.g.state['L'] != L + 1: continue
        except NavError: continue
        if descend_to(p, tx, ty, tz, tried): return True
        if p.g.state['L'] == L + 1:                                   # back up the way we came: the up ladder we can reach, nearest us
            here = reach_set(p); cx, cy = p.nav.pos()
            up = [u for u in ladders() if u[4] == L + 1 and u[0] in (305, 106) and u[1] != 0 and beside(here, u[2], u[3])]
            up = sorted(up, key=lambda u: abs(u[2] - cx) + abs(u[3] - cy))
            try:
                if not up or not use_way(p, {'obj': up[0][0], 'x': up[0][2], 'y': up[0][3]}): return False
            except NavError: return False
    return False

def ascend_to(p, tx, ty, tz, tried=None):
    """from a dungeon level, take ladders UP (to a lower level number) until (tx,ty) on level tz can be walked to;
    ladders nearest the target first, back down to try the next when one leads nowhere. True if there"""
    tried = set() if tried is None else tried
    L = p.g.state['L']; reach = reach_set(p)
    if L == tz: return beside(reach, tx, ty)
    ups = [(o, fr, x, y) for o, fr, x, y, z in ladders() if z == L and o in (305, 106) and fr != 0]
    ups = sorted((u for u in ups if beside(reach, u[2], u[3]) and (u[2], u[3], L) not in tried), key=lambda u: abs(u[2] - tx) + abs(u[3] - ty))
    for o, fr, x, y in ups[:6]:
        tried.add((x, y, L))
        p.log('  L%d: up at (%d,%d) toward (%d,%d,%d)' % (L, x, y, tx, ty, tz))
        try:
            if not use_way(p, {'obj': o, 'x': x, 'y': y}) or p.g.state['L'] != L - 1: continue
        except NavError: continue
        if ascend_to(p, tx, ty, tz, tried): return True
        if p.g.state['L'] == L - 1:                                   # back down the way we came
            here = reach_set(p); cx, cy = p.nav.pos()
            dn = sorted((d for d in ladders() if d[4] == L - 1 and d[0] in (305, 106) and d[1] == 0 and beside(here, d[2], d[3])), key=lambda d: abs(d[2] - cx) + abs(d[3] - cy))
            try:
                if not dn or not use_way(p, {'obj': dn[0][0], 'x': dn[0][2], 'y': dn[0][3]}): return False
            except NavError: return False
    return False

def unlock_any(p, x, y, magic=False):
    """the locked door beside the leader at (x,y): Unlock Magic (Ex Por) on a magical lock; else a key of its
    quality, else a lock pick (quality 0 only; one may break, usecode_use.md)"""
    px, py = p.nav.pos(); d = VDIR[(x - px, y - py)]
    if magic:
        st = p.key('c', 'cast')
        if not st['cast']: p.idle(); return False
        p.g.type('ep'); st = p.at(d, 'unlock magic'); p.idle(); p.log('  unlock magic (%d,%d): %r' % (x, y, st['msg'][-40:]))
        return 'unlocked' in st['msg']
    if unlock_with_key(p, x, y): return True
    for _ in range(5):
        st = p.use_pack(63)
        if st is None: return False
        st = p.at(d, 'lock pick') if st.get('verb') else st; p.idle(); p.log('  lock pick (%d,%d): %r' % (x, y, st['msg'][-40:]))
        if 'unlocked' in st['msg']: return True
    return False

def unlock_with_key(p, x, y):
    """U)se each carried key on the locked door beside the leader at (x,y) until one fits (a lock's key is
    the key with its quality, usecode_use.md); True when unlocked"""
    px, py = p.nav.pos(); d = VDIR[(x - px, y - py)]
    nkeys = sum(1 for r in p.g.objects()[1] if r['obj'] == 64)
    for k in range(nkeys):
        p.key('u'); seen = 0; st = p.g.state
        for _ in range(len(p.g.objects()[1]) + 4):                    # the pack prompt: the k-th key
            st = p.key('u')
            if st['inv'] and st['msg'].startswith('Use the key'):
                seen += 1
                if seen > k: break
        st = p.key('ret', 'the key'); st = p.at(d, 'the key on the door') if st['verb'] else st
        p.log('  key %d on (%d,%d): %r' % (k, x, y, st['msg'][-60:]))
        if 'unlocked' in st['msg']: return True
        p.idle()
    return False

def blue_bottle(p):
    """into the Moonglow catacombs: the Blue Bottle's locked door (Shamino's lockpick), the milk bottle onto the
    table, L)ook finds the secret door, the east barrel onto the ladder, the middle one east, a plant into the
    opened west barrel, the ladder's barrel into the plant's place, then U)se the ladder at (942,521) from the
    corner (bot_walkthrough.md §3). The Avatar alone, so no one stands in the way"""
    p.walk(938, 525)
    for _ in range(8): p.key('space', 'the party gathers')               # party mode later needs everyone within 8 tiles
    ids = [m['id'] for m in p.g.state['party']]                          # a pick breaks on d30 < (45-dex)/2: Shamino's hands
    who = ids.index(3) + 1 if 3 in ids else 1
    st = p.key(str(who), 'solo'); p.log('  solo %d: %r' % (who, st['msg'][-40:])); p.nav.refresh()
    p.walk(938, 525)
    for _ in range(5):                                                   # a pick may break
        st = p.use_pack(63); st = p.at('right', 'the lockpick') if st and st['verb'] else st
        p.log('  lockpick: %r' % (st and st['msg'][-60:]))
        if st is None or 'unlocked' in st['msg'] or 'opened' in st['msg']: break
    st = p.act('u', 'right', 'the door'); p.log('  door: %r' % st['msg'][-60:])
    st = p.key('0', 'party mode')                                        # (in solo the roster shows the soloist first: back to the party, then 1)
    if 'Party mode' not in st['msg']: p.issue('honesty', 'back to the party: %r' % st['msg'][-60:])
    st = p.key('1', 'the Avatar alone')
    p.nav.refresh(); p.walk(941, 525)                                    # the room is too small for a party
    steps = [('move', 'up', 'right'),                                    # the milk bottle onto the table
             ('key', 'up'), ('look', 'up'), ('key', 'up'),               # find the secret door, stand in it (941,523)
             ('move', 'upright', 'up'),                                  # the east barrel onto the ladder's tile
             ('move', 'up', 'right'),                                    # the middle barrel east, into its place
             ('key', 'up'),                                              # (941,522)
             ('use', 'left'),                                            # open the west barrel
             ('move', 'up', 'downleft'),                                 # the plant north of us into it
             ('move', 'upright', 'left')]                                # the barrel off the ladder, where the plant was
    for s_ in steps:                                                     # (as done in DOS U6, 2026-11-07)
        if s_[0] == 'key': st = p.key(s_[1])
        elif s_[0] == 'look': p.key('l'); st = p.at(s_[1], 'look')
        elif s_[0] == 'use': st = p.act('u', s_[1])
        else: p.act('m', s_[1]); st = p.key(s_[2], 'to')
        p.log('  %s: (%d,%d) %r' % ('/'.join(s_), st['X'], st['Y'], st['msg'][-60:]))
    if (p.g.state['X'], p.g.state['Y']) != (941, 522): p.issue('honesty', 'the Blue Bottle way stopped at (%d,%d)' % (p.g.state['X'], p.g.state['Y'])); p.key('0'); return False
    st = p.key('0', 'party mode')
    if 'Party mode' not in st['msg']: p.issue('honesty', 'party mode at the ladder: %r' % st['msg'][-60:])
    for _ in range(6): p.key('space', 'the party comes')                 # they walk in behind
    st = p.act('u', 'upright', 'the ladder'); p.log('  ladder: L%d (%d,%d) %r' % (st['L'], st['X'], st['Y'], st['msg'][-60:]))
    if st['L'] != 1: p.issue('honesty', 'the catacombs ladder did not lead down: %r' % st['msg'][-80:]); return False
    p.nav.refresh(); return True

def seg_honesty(p, kw):
    """Moonglow: Manrel's crypt key; the catacombs (the Blue Bottle's back room) down to Beyvin's body on
    level 2 at (219,147), which holds the rune of Honesty"""
    if not orb_go(p, 1): return
    p.talk_to(40, ['beyv', 'y', 'y'], 'Manrel', yes=True)
    if not carried(p, 64): p.issue('honesty', 'Manrel gave no key')
    if not blue_bottle(p): return
    if not descend_to(p, 219, 151, 2): p.issue('honesty', 'no way down to the crypt door (219,150) level 2 from L%d (%d,%d)' % (p.g.state['L'], p.g.state['X'], p.g.state['Y'])); return
    p.walk(219, 151)
    if not unlock_with_key(p, 219, 150): p.issue('honesty', "the crypt door would not unlock with any key: %r" % p.g.state['msg'][-80:]); return
    st = p.act('u', 'up', 'open the crypt'); p.log('  crypt: %r' % st['msg'][-60:]); p.nav.refresh()
    p.walk(219, 147, adj=True); px, py = p.nav.pos()
    st = p.use_pack(90); p.log('  torch: %r' % (st and st['msg'][-60:]))      # the crypt is dark: L)ook finds nothing in the dark
    p.key('l', 'look'); st = p.at(VDIR[(219 - px, 147 - py)], 'search the body'); p.log('  body: %r' % st['msg'][-140:])
    get_at(p, 219, 147, 242, tries=6)
    got_rune(p, 242, 'Beyvin')

# the eight shrines (usecode_use.md runes; moongates.md §3): virtue -> the field's tile (the moonstone lies under
# it), its level, the Orb position, the mantra. The rune is 242 + virtue; the field (51) has quality = virtue.
SHRINES = [(0, (935, 263), 0, 2, 'ahm'), (1, (503, 359), 0, 4, 'mu'), (2, (159, 943), 0, 10, 'ra'), (3, (295, 39), 0, 20, 'beh'),
           (4, (831, 167), 0, 24, 'cah'), (5, (327, 823), 0, 22, 'summ'), (6, (23, 23), 1, 16, 'om'), (7, (919, 935), 0, 6, 'lum')]

def ready(p, obj_n, member=1):
    """E)quip: E steps through the pack until it names the thing, a digit readies it on that member (equipment.md)"""
    names = [r.get('name', '') for r in p.g.objects()[1] if r['obj'] == obj_n]
    if not names: return False
    p.idle(); st = p.key('e', 'equip')
    for _ in range(len(p.g.objects()[1]) + 2):
        if names[0] and names[0] in st['msg']: break
        st = p.key('e', 'next')
    n0 = len(names)
    st = p.key(str(member), 'ready'); p.idle(); p.log('  ready %s on %d: %r' % (names[0], member, st['msg'][-60:]))
    return sum(1 for r in p.g.objects()[1] if r['obj'] == obj_n) < n0

def seg_shrines(p, kw):
    """every shrine: the Orb there, a step to the force field, the mantra on the rune, the moonstone, and away by
    the next Orb trip before the guardians close in (the Avatar wears the invisibility ring from the mouse nook;
    the walkthroughs: the guardians need not be fought)"""
    ring = carried(p, 258) and ready(p, 258)
    p.log('  invisibility ring readied: %s' % ring)
    for v, (fx, fy), lv, pos, mantra in SHRINES:
        def have(): return any(r['obj'] == 73 and r.get('fr', -1) == v for r in p.g.objects()[1])
        for attempt in range(3):
            if have(): break
            p.log('  shrine %d: orb %d, field (%d,%d) L%d (try %d)' % (v, pos, fx, fy, lv, attempt + 1))
            if not orb_go(p, pos): continue
            def field(): return [o for o in p.g.objects()[0] if o['obj'] == 51 and (o['x'], o['y']) == (fx, fy)]
            try:                                         # the field is a step from the gate: in, the mantra, the stone, out
                if field():
                    p.nav.goto(fx, fy, adj=True, max_steps=40)
                    m = use_rune(p, 242 + v, mantra); p.log('  rune %d %r: %r' % (242 + v, mantra, m))
                    if field(): p.issue('shrines', 'shrine %d: the field stayed after %r: %r' % (v, mantra, m))
                if not field(): get_at(p, fx, fy, 73, tries=2)
            except (NavError, Danger) as e: p.log('  shrine %d: %s' % (v, e))
            if have(): break                             # leave the guardians be; the next Orb trip takes us away
            if p.foes(): p.fight(); p.heal_up()
            p.cure_up(); p.rest_if_hurt(0.7)
    have = sorted(r.get('fr', -1) for r in p.g.objects()[1] if r['obj'] == 73)
    p.log('  moonstones carried: %s' % have)
    if len(have) != 8: p.issue('shrines', 'moonstones carried: %s' % have)

def wait_until(p, h0, h1, what):
    """pass turns (fighting what comes) until the clock is between h0 and h1 o'clock"""
    for t in range(1500):
        if h0 <= p.g.state['min'] // 60 < h1: return True
        if t % 20 == 0 and (p.poisoned() or p.near_death(0.5)): p.cure_up(); p.heal_up(0.5)   # poison eats away the waiting party
        if p.g.state['foes'] and p.foes(): p.fight(); p.heal_up(); continue
        p.key('space', what)
    return False

def go_to_actor(p, aid, name):
    """down (or across) to actor aid's level and beside them; True if near"""
    w = p.g.where(aid)
    if not w: p.issue('actor', '%s (%d) is nowhere' % (name, aid)); return False
    x, y, z = w; p.log('  %s at (%d,%d) L%d' % (name, x, y, z))
    if z != p.g.state['L'] and not descend_to(p, x, y, z):
        p.issue('actor', 'no way to %s at (%d,%d) L%d from L%d (%d,%d)' % (name, x, y, z, p.g.state['L'], p.g.state['X'], p.g.state['Y'])); return False
    return p.approach(aid) is not None

def seg_gargish(p, kw):
    """Hythloth (the cave at (948,930) by the Shrine of Humility) down to Captain John on level 4: his scroll
    teaches Gargish; on to the way out into the Gargoyle lands (220,219) level 5, where Beh Lem waits at noon"""
    if (min(count(p, 68), count(p, 71)) < 30 or count(p, 72) < 4) and p.g.state.get("gold", 0) >= 60: restock(p, 30)   # the Avatar's heals carry him through; ash for Light
    for obj in (68, 71, 67):
        for _ in range(4):
            if count(p, obj) == count(p, obj, 1) or not give_to_avatar(p, obj, others=True): break
    lb_heal(p)                                                           # in at full health
    p.log('  into Hythloth: %s; ginseng %d silk %d' % (party_line(p), count(p, 68, 1), count(p, 71, 1)))
    if not orb_go(p, 6): return
    if carried(p, 258): ready(p, 258)                                    # unseen, the Avatar walks past what lives in Hythloth
    p.sneak = carried(p, 258)                                            # seen: fight the drakes, walking on under their fireballs kills the party
    p.walk(948, 930)
    if p.g.state['L'] != 1: p.issue('gargish', 'the Hythloth cave did not lead down: L%d (%d,%d)' % (p.g.state['L'], p.g.state['X'], p.g.state['Y'])); return
    cast_light(p) or light_torch(p)                                      # the dungeon is dark: spells find 'nothing' on whom they can't see
    if not go_to_actor(p, 183, 'Captain John'): return
    _, inv0 = p.g.objects(); before = {r['obj'] for r in inv0}
    p.talk_to(183, ['lang', 'y', 'y'], 'Captain John', yes=True)
    _, inv1 = p.g.objects(); new = [r for r in inv1 if r['obj'] not in before]
    p.log('  from John: %s' % [(r['obj'], r.get('name')) for r in new])
    for r in new: st = p.use_pack(r['obj']); p.log('  use %s: %r' % (r.get('name'), st and st['msg'][-120:]))
    if not descend_to(p, 220, 219, 5): p.issue('gargish', 'no way down to the Gargoyle lands (220,219) L5'); return
    p.walk(220, 219, near=2)
    wait_until(p, 12, 14, 'wait for noon')
    if not go_to_actor(p, 164, 'Beh Lem'): return
    p.talk_to(164, ['join', 'y'], 'Beh Lem', yes=True)
    p.sneak = False
    if 164 not in [m['id'] for m in p.g.state['party']]: p.issue('gargish', 'Beh Lem did not join')

TWO_HANDED = {41, 42, 44, 45, 46, 47, 50, 54}                           # bows, crossbows, two-handed blades: both hands (equipment.md)

def cast_light(p):
    """C)ast Light (In Lor, sulfurous ash): 100 minutes of light (magic_skills.md timers)"""
    if not count(p, 72, 1): return None
    st = p.key('c', 'cast')
    if not st['cast']: p.idle(); return None
    st = p.g.type('il'); p.idle(); p.log('  light: %r' % st['msg'][-50:])
    return 'success' in st['msg'].lower()

def avatar_leads(p, turns=30):
    """wait (fighting what comes) until the Avatar is the one played again: asleep, paralyzed or knocked down, the
    next member takes the keys, and a spell would be his (and his reagents)"""
    for _ in range(turns):
        party = p.g.state['party']
        if party and party[0]['id'] == 1: return True
        if p.g.state['foes'] and p.foes(): p.fight(); continue
        p.key('space', 'the Avatar comes round')
    return False

def dispel_at(p, x, y, d):
    """from (x,y), C)ast Dispel Field (An Grav) on the next tile that way (magic_skills.md §5a)"""
    p.walk(x, y); avatar_leads(p)
    st = p.key('c', 'cast')
    if not st['cast']: p.idle(); return False
    p.g.type('ag'); st = p.at(d, 'dispel the field'); p.idle()
    p.log('  dispel field %s of (%d,%d): %r' % (d, x, y, st['msg'][-60:])); p.nav.refresh()
    return 'Success' in st['msg']

def give_to(p, obj_n, who):
    """M)ove every row of obj_n someone else carries to member who: M through the pack, Enter, the count for a
    stack, the crosshair on them, Enter (equipment.md §5 M)ove)"""
    for _ in range(4):
        rows = [r for r in p.g.objects()[1] if r['obj'] == obj_n and r.get('own', 1) != who]
        if not rows: return True
        inv = p.g.objects()[1]; name = rows[0].get('name', ''); at = rows[0].get('row', 0)
        p.idle(); p.key('m', 'move')
        for k in range(len(inv) + 2):                                    # the pack steps in row order: to that row, not an earlier one
            st = p.key('m', 'the pack')
            if st['msg'].startswith('Move the ' + name) and k >= at: break
        else: p.idle(); return False
        st = p.key('ret', 'this one')
        if 'How many' in st['msg']: p.g.type(str(rows[0]['qty'] or 1))
        st = p.aim_at(who, 'to the holder'); p.idle()
        p.log('  %s to %d: %r' % (name, who, st['msg'][-40:]))
        if 'nobody' in st['msg']: return False
    return False

def light_torch(p):
    """a torch lit in a free hand: the member carrying it holds it (usecode_use.md), so the torches go first to
    someone with a hand free (slots 3, 4 in @E), the whole stack by M)ove and the crosshair, then U)se"""
    if not carried(p, 90): return None
    eq = readied(p); ids = [m['id'] for m in p.g.state['party'] if m['hp'] > 0]
    free = [i for i in ids if not ((eq.get((i, 3)) and eq.get((i, 4))) or eq.get((i, 3)) in TWO_HANDED) and i != 9]   # Sherry the mouse holds nothing
    if not free: p.log('  torch: no free hand (%s)' % sorted(eq.items())); return None
    who = free[0]
    give_to(p, 90, who)
    st = p.use_pack(90)
    p.log('  torch for %d (%d carried): %r' % (who, count(p, 90), st and st['msg'][-60:]))
    return st

def seg_gargoyles(p, kw):
    """the Gargoyle lands, Beh Lem along: Draxinusom (surrender; the Amulet of Submission), Naxatilor (the
    Vortex Lens and the Book of Ritual), the Hall of Knowledge south of them (the broken lens 395 at (124,194),
    the four books in the Librum chamber), the lensmaker Lor-wis-lem to the north-east (he mends it), and
    Naxatilor again: a second, human-made concave lens is needed (Ephemerides)"""
    p.loot_ok = False
    if not go_to_actor(p, 167, 'Draxinusom'): return
    p.talk_to(167, ['surr', 'y', 'y', 'y', 'sacr', 'y'], 'Draxinusom', yes=True)
    eq = readied(p)
    p.log('  amulet: readied %s, carried %s' % (any(v == 76 for v in eq.values()), carried(p, 76)))
    if not carried(p, 76) and not any(v == 76 for v in eq.values()): p.issue('gargoyles', 'no Amulet of Submission from Draxinusom'); return
    if carried(p, 76): ready(p, 76)                                     # worn, the gargoyles talk
    if not go_to_actor(p, 168, 'Naxatilor'): return
    p.talk_to(168, ['vort', 'lens', 'bye'], 'Naxatilor', yes=True)
    dispel_at(p, 127, 183, 'down')                                       # the Hall's door holds a protection field (319)
    drop_junk(p)                                                         # room for the lens: the Avatar carries at most twice his strength
    for obj in (73, 90): give_to(p, obj, 2)                              # the moonstones and torches go to Dupre
    if not carried(p, 395) and not get_at(p, 124, 194, 395): p.issue('gargoyles', 'no broken lens at (124,194) L5'); return
    dispel_at(p, 124, 181, 'left')                                       # the Librum chamber's door holds a field too (123,181)
    for bx, by in ((115, 183), (120, 183), (115, 187), (120, 187)):        # the Librum chamber: the Book of Ritual is one of them
        p.walk(bx, by, adj=True); px, py = p.nav.pos()
        p.key('l', 'look'); st = p.at(VDIR[(bx - px, by - py)], 'read the book')
        p.log('  book (%d,%d): %r' % (bx, by, st['msg'][:100])); p.idle()
        for _ in range(12):                                              # a long text pages on with a key
            if not p.g.state.get('more'): break
            p.key('space', 'page')
    if not go_to_actor(p, 170, 'the lensmaker'): return
    p.talk_to(170, ['lens', 'bye'], 'the lensmaker', yes=True)
    lens = [o for o in (394, 396) if carried(p, o)]
    p.log('  lenses: %s, broken %s' % (lens, carried(p, 395)))
    if not lens: p.issue('gargoyles', 'the lensmaker did not mend the lens'); return
    if not go_to_actor(p, 168, 'Naxatilor'): return
    out = p.talk_to(168, ['y', 'lens', 'bye'], 'Naxatilor', yes=True)
    if out and not any('second lens' in r for _, r in out): p.issue('gargoyles', 'Naxatilor never asked for the second lens: %r' % out[-2:])

def climb_out(p):
    """up the nearest reachable ladder until the surface"""
    for _ in range(6):
        L = p.g.state['L']
        if L == 0: return True
        reach = reach_set(p); x, y = p.nav.pos()
        up = sorted((u for u in ladders() if u[4] == L and u[0] in (305, 106) and u[1] != 0 and beside(reach, u[2], u[3])),
                    key=lambda u: abs(u[2] - x) + abs(u[3] - y))
        if not up or not use_way(p, {'obj': up[0][0], 'x': up[0][2], 'y': up[0][3]}): return False
        p.nav.refresh()
    return p.g.state['L'] == 0

def seg_lenses(p, kw):
    """Ephemerides the astronomer by the Lycaeum keeps the Gargoyle Lens to study and wants a glass sword to cast
    a concave copy; the glass sword lies in remains at (201,135) on the Moonglow catacombs' first level (the Blue
    Bottle's back room, opened in the honesty chapter); he returns both lenses (394 concave, 396 convex)"""
    p.loot_ok = False
    if not carried(p, 48):
        if not orb_go(p, 1): return
        p.walk(941, 522)                                                 # the back room is as the honesty chapter left it
        for _ in range(6): p.key('space', 'the party comes')
        st = p.act('u', 'upright', 'the ladder'); p.log('  ladder: L%d (%d,%d) %r' % (st['L'], st['X'], st['Y'], st['msg'][-60:]))
        if st['L'] != 1: p.issue('lenses', 'the catacombs ladder did not lead down: %r' % st['msg'][-80:]); return
        p.nav.refresh(); cast_light(p)
        p.walk(201, 135, adj=True); px, py = p.nav.pos()
        p.key('l', 'look'); st = p.at(VDIR[(201 - px, 135 - py)], 'search the remains'); p.log('  remains: %r' % st['msg'][-120:])
        if not get_at(p, 201, 135, 48, tries=4): p.issue('lenses', 'no glass sword in the remains at (201,135) L1'); return
        if not climb_out(p): p.issue('lenses', 'no way up from L%d (%d,%d)' % (p.g.state['L'], p.g.state['X'], p.g.state['Y'])); return
        if p.nav.pos() == (942, 521):                                    # the ladder's tile: barrels west and south, and no diagonal between them
            ids = [m['id'] for m in p.g.state['party']]
            seq = [('k', str(ids.index(2) + 1) if 2 in ids else '2')]   # Dupre alone out of the corner first: he stands where the barrel must pass
            seq += [('k', 'down')] * 3 + [('k', 'left'), ('k', '0'), ('k', '1')]   # down the passage to (940,525); party, then the Avatar alone
            seq += [('m', 'down', 'downleft'), ('k', 'down'), ('k', 'left')] + [('m', 'down', 'down'), ('k', 'down')] * 3 + [('k', '0')]
            for s_ in seq:                                               # the south barrel into the secret doorway, then south down the passage
                if s_[0] == 'm': p.act('m', s_[1]); st = p.key(s_[2], 'push')
                else: st = p.key(s_[1])
                p.log('  %s: (%d,%d) %r' % ('/'.join(s_), st['X'], st['Y'], st['msg'][-40:]))
            p.idle(); p.nav.refresh()
    wait_until(p, 15, 20, 'Ephemerides is up 15-20')                     # SCHEDULE: asleep 5-15, at his telescope by night
    if not go_to_actor(p, 35, 'Ephemerides'): return
    p.talk_to(35, ['lens', 'bye'], 'Ephemerides', yes=True)
    p.log('  lens kept: %s' % (not carried(p, 396)))
    out = p.talk_to(35, ['swor', 'bye'], 'Ephemerides', yes=True)
    p.log('  lenses now: concave %s convex %s, sword %s' % (carried(p, 394), carried(p, 396), carried(p, 48)))
    if not (carried(p, 394) and carried(p, 396)): p.issue('lenses', 'Ephemerides did not give both lenses: %r' % (out and out[-2:]))

def region(p, sx, sy):
    """the walkable tiles reachable on foot from (sx,sy) on the surface pass map"""
    p.nav.ensure(); S, pm = p.nav.span, p.nav.pm
    seen = {(sx, sy)}; q = [(sx, sy)]
    while q:
        x, y = q.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = ((x + dx) % S, (y + dy) % S)
            if n not in seen and (pm[n[1] * S + n[0]] & 1 or n in p.nav.doors): seen.add(n); q.append(n)
    return seen

def sail_to(p, land, tries=8):
    """aboard a skiff: over water (pass-map bit 8) to a water tile beside a tile of `land`, an arrow key a step
    (vehicles.md, wind.md §2), then U)se to step ashore (§3.1); a tile that refuses the boat (another boat on
    it) is routed around; True when ashore on `land`"""
    import collections
    S, pm = p.nav.span, p.nav.pm
    land = {t for t in land if not pm[t[1] * S + t[0]] & 8}           # a boat moored on water is walkable, but not land
    stop = set()
    for _ in range(tries):
        stop |= {(o['x'], o['y']) for o in p.g.objects()[0] if o['obj'] in (412, 414, 415)}   # boats near us now (the dump reaches 24 tiles)
        x0, y0 = p.nav.pos(); stop.discard((x0, y0))
        if any(((x0 + a) % S, (y0 + b) % S) in land for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1))):   # beside the shore already
            p.key('u'); st = p.key('ret', 'ashore'); p.log('  ashore: %r at (%d,%d)' % (st['msg'][-40:], st['X'], st['Y'])); p.nav.refresh()
            if p.nav.pos() not in land: p.issue('sail', 'not ashore at (%d,%d): %r' % (st['X'], st['Y'], st['msg'][-60:]))
            return p.nav.pos() in land
        came = {(x0, y0): None}; q = collections.deque([(x0, y0)]); end = None
        while q and end is None:
            x, y = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = ((x + dx) % S, (y + dy) % S)
                if n in came or n in stop or not pm[n[1] * S + n[0]] & 8: continue
                came[n] = (x, y); q.append(n)
                if any(((n[0] + a) % S, (n[1] + b) % S) in land for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1))): end = n; break
        if end is None: p.issue('sail', 'no water route from (%d,%d) to the shore' % (x0, y0)); return False
        path = []; c = end
        while c != (x0, y0): path.append(c); c = came[c]
        path.reverse(); p.log('  sailing %d tiles from (%d,%d) to (%d,%d)' % (len(path), x0, y0, end[0], end[1]))
        for nx, ny in path:
            for _ in range(4):                                           # a step may cost more than a turn against the current
                x, y = p.nav.pos()
                if (x, y) == (nx, ny): break
                p.key(VDIR[((nx - x + S // 2) % S - S // 2, (ny - y + S // 2) % S - S // 2)], 'sail')
            if p.nav.pos() != (nx, ny): stop.add((nx, ny)); p.log('  (%d,%d) refuses the skiff: around it' % (nx, ny)); break
        else:
            p.key('u'); st = p.key('ret', 'ashore')                       # U)se on the boat's own tile: leaving is the use
            p.log('  ashore: %r at (%d,%d)' % (st['msg'][-40:], st['X'], st['Y'])); p.nav.refresh()
            if p.nav.pos() not in land: p.issue('sail', 'not ashore at (%d,%d): %r' % (st['X'], st['Y'], st['msg'][-60:]))
            return p.nav.pos() in land
    p.issue('sail', 'the skiff stuck at (%d,%d): %r' % (p.nav.pos() + (p.g.state['msg'][-60:],))); return False

def get_key_from_papa(p, valley):
    """the fishing pole at (607,291), a fish at the shore, and Papa trades the cellar key for it"""
    if not carried(p, 264) and not get_at(p, 607, 291, 264): p.issue('cube', 'no fishing pole at (607,291)'); return
    for _ in range(3):
        if carried(p, 265): break
        x, y = p.nav.pos()                                               # back to the water: the shore we landed on
        shore = min((t for t in valley if any(p.nav.pm[(t[1] + b) * p.nav.span + t[0] + a] & 8 for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1)))),
                    key=lambda t: abs(t[0] - 607) + abs(t[1] - 296))
        p.walk(*shore)
        for _ in range(40):
            st = p.use_pack(264); p.log('  fishing: %r' % (st and st['msg'][-40:]))
            if carried(p, 265) or (st and 'water' in st['msg']): break
    if not carried(p, 265): p.issue('cube', 'caught no fish'); return
    wait_until(p, 7, 19, 'Papa is up')
    if not go_to_actor(p, 143, 'Papa'): return
    nkeys = count(p, 64)
    out = p.talk_to(143, ['key', 'y', 'bye'], 'Papa', yes=True)                # "Got key. Want key?" - yes, for the fish
    p.log('  keys %d -> %d: %r' % (nkeys, count(p, 64), out and out[-2:]))
    if count(p, 64) <= nkeys: p.issue('cube', 'Papa gave no key for the fish'); return

def seg_cube(p, kw):
    """Stonegate, the cyclopes' castle in a valley that opens only on Lost Hope Bay: Trebor in Minoc sells the deed
    to his skiff (595,119); sail south to the valley's shore; the fishing pole at (607,291) in Stonegate; a fish;
    Papa (143) gives the cellar key for it; down the cellar to the vortex cube at (147,57) level 4"""
    p.loot_ok = False
    there = p.g.state['L'] == 0 and not p.g.state.get('veh') and p.nav.pos() in region(p, 607, 296)
    if not there and not carried(p, 149) and not p.g.state.get('veh'):
        if not orb_go(p, 25): return
        wait_until(p, 8, 13, 'Trebor sells by day')
        if not go_to_actor(p, 72, 'Trebor'): return
        ids = [m['id'] for m in p.g.state['party']]
        out = p.talk_to(72, ['skif', '1', 'y', 'bye'], 'Trebor', yes=True)
        p.log('  deed: %s (%r)' % (carried(p, 149), out and out[-2:]))
        if not carried(p, 149): p.issue('cube', 'no skiff deed from Trebor'); return
    if not there and not p.g.state.get('veh'):
        p.walk(595, 118); st = p.act('u', 'down', 'board the skiff'); p.log('  skiff: %r veh=%s' % (st['msg'][-40:], st.get('veh')))
        if not p.g.state.get('veh'): p.issue('cube', 'could not board the skiff at (595,119): %r' % st['msg'][-60:]); return
    valley = region(p, 607, 296)
    if not there and not sail_to(p, valley): return
    has_key = lambda: any(r['obj'] == 64 and r.get('q') == 13 for r in p.g.objects()[1])   # Papa's key fits the cellar (quality 13)
    if not has_key(): get_key_from_papa(p, valley)
    if not has_key(): return
    if p.g.state['L'] == 0:                                              # the centre ladder (607,287) is behind a secret door (334 at (607,286))
        p.walk(607, 285); p.key('l', 'look'); st = p.at('down', 'search'); p.log('  search (607,286): %r' % st['msg'][-60:]); p.idle(); p.nav.refresh()
        p.walk(607, 286); st = p.act('u', 'down', 'the centre ladder')    # its cell holds the four levers (268, q11-14) of the cells' portcullises
        if st['L'] == 1:
            fr = {(o['x'], o['y']): o['fr'] for o in p.g.objects()[0] if o['obj'] == 268}
            for d, t in (('up', (151, 70)), ('left', (150, 71)), ('right', (152, 71)), ('down', (151, 72))):
                if fr.get(t, 0) != 0: continue                           # pulled already (frame 1): a second pull closes them again
                st = p.act('u', d, 'lever'); p.log('  lever %s: %r' % (d, st['msg'][-50:]))
            p.key('u'); st = p.key('ret', 'climb'); p.log('  up: L%d %r' % (st['L'], st['msg'][-40:])); p.nav.refresh()
    if not descend_to(p, 151, 68, 4): p.issue('cube', 'no way down to the switch (151,69) L4'); return   # the castle's ladders: one cell leads on
    cast_light(p)
    p.walk(151, 68); st = p.act('u', 'down', 'the switch'); p.log('  switch (151,69): %r' % st['msg'][-60:]); p.nav.refresh()   # 174 q105: the electric fields (150,67), (147,70) go
    p.walk(147, 65); p.key('l', 'look'); st = p.at('up', 'search'); p.log('  search (147,64): %r' % st['msg'][-60:]); p.idle(); p.nav.refresh()   # a secret door (334)
    p.walk(147, 63)
    if not unlock_with_key(p, 147, 62): p.issue('cube', 'the cellar door (147,62) L4 would not unlock: %r' % p.g.state['msg'][-60:]); return   # Papa's key, quality 13
    st = p.act('u', 'up', 'open the cellar'); p.log('  cellar door: %r' % st['msg'][-40:]); p.nav.refresh()
    dispel_at(p, 147, 59, 'up')                                          # protection fields (319) ring the cube: one dispelled opens it
    if not get_at(p, 147, 57, 62, tries=4): p.issue('cube', 'no vortex cube at (147,57) L4'); return
    climb_out(p)

def board_skiff(p, near=None):
    """walk to the nearest skiff (414) beside our land and U)se it (vehicles.md §3); near: where it was left (the
    object dump reaches 24 tiles); True if aboard"""
    if p.g.state.get('veh'): return True
    if near: p.walk(*near)
    x, y = p.nav.pos(); here = region(p, x, y)
    boats = sorted((o for o in p.g.objects()[0] if o['obj'] == 414), key=lambda o: abs(o['x'] - x) + abs(o['y'] - y))
    for b in boats:
        side = [(b['x'] + a, b['y'] + c) for a, c in ((1, 0), (-1, 0), (0, 1), (0, -1)) if (b['x'] + a, b['y'] + c) in here]
        if not side: continue
        p.walk(*side[0]); px, py = p.nav.pos()
        st = p.act('u', VDIR[(b['x'] - px, b['y'] - py)], 'board the skiff'); p.log('  skiff (%d,%d): %r' % (b['x'], b['y'], st['msg'][-40:]))
        if p.g.state.get('veh'): return True
    p.issue('sail', 'no skiff to board near (%d,%d)' % (x, y)); return False

def seg_keg(p, kw):
    """to Sutek's island (east of Serpent's Hold) with a powder keg for its locked gate. A keg lying in a town is
    someone's (taking it is theft: the guards come, usecode_use.md), so Efram in Britain sells one. The skiff
    sails from Stonegate's shore to Minoc and waits there; the Orb to Britain and back; then the skiff south round
    the coast to the beach below the castle"""
    p.loot_ok = False
    minoc = lambda: {t for t in region(p, 601, 106) if abs(t[0] - 601) + abs(t[1] - 105) <= 20}
    if not carried(p, 223):
        if p.g.state['L'] == 0 and p.nav.pos() in region(p, 607, 296):     # in Stonegate's valley: the skiff to Minoc first
            if not board_skiff(p, (597, 202)): return
            if not sail_to(p, minoc()): return
            p.skiff_at = p.nav.pos()
        if not orb_go(p, 3): return                                       # east of Britain: the castle's gate (a portcullis, its lever behind a locked door) shuts the throne room in
        wait_until(p, 6, 19, 'Efram keeps shop 6-20')
        if not go_to_actor(p, 22, 'Efram'): return
        lighten(p)                                                        # the gold is the Avatar's purse: he buys, so room for three stones
        out = p.talk_to(22, ['buy', 'powd', '1', '1', 'bye'], 'Efram')
        p.log('  keg: %s (%r)' % (carried(p, 223), out and out[-2:]))
        if not carried(p, 223): p.issue('keg', 'Efram sold no powder keg'); return
        if not orb_go(p, 25): return
    if not board_skiff(p, getattr(p, 'skiff_at', (605, 119))): return
    sail_to(p, {t for t in region(p, 787, 970) if abs(t[0] - 787) + abs(t[1] - 970) <= 20})   # the beach below the gate

def shed(p):
    """leave what the quest no longer needs (the load limit is twice strength, reagents a tenth of a stone each):
    the decorations from Sutek's wall, flowers, the fishing pole, oil, torches beyond four, garlic beyond twenty"""
    spot = p.open_spot(4)                                                # somewhere a thing can land (not the Orb's gate in the throne room)
    if spot: p.walk(*spot)
    for obj, keep in ((141, 0), (140, 0), (139, 0), (264, 0), (83, 0), (90, 4), (67, 20)):
        for _ in range(4):
            rows = [r for r in p.g.objects()[1] if r['obj'] == obj and r.get('own', 1) == 1]
            n = sum(r['qty'] or 1 for r in rows)
            if n <= keep: break
            if not drop_here(p, obj, min(n - keep, rows[0]['qty'] or 1)): break

def drop_here(p, obj_n, n=1):
    """D)rop n of obj_n at the leader's feet: D, D through the pack to it, Enter, Enter (a stack asks how many)"""
    inv = p.g.objects()[1]; rows = [r for r in inv if r['obj'] == obj_n]
    if not rows: return False
    name = rows[0].get('name', ''); n0 = count(p, obj_n)
    p.idle(); p.key('d', 'drop')
    for _ in range(len(inv) + 2):
        st = p.key('d', 'next')
        if ('Drop-' + name) in st['msg']: break
    else: p.idle(); return False
    p.key('ret'); st = p.key('ret', 'at the feet')
    if 'How many' in st['msg']: p.g.type(str(n)); st = p.g.state
    if 'Location' in st['msg']: st = p.key('ret', 'here')
    p.log('  drop %s: %r' % (name, st['msg'][-50:])); p.idle()
    return count(p, obj_n) < n0

def seg_gate(p, kw):
    """Sutek's gate (787,964) is locked past any key (quality 255): the powder keg blows it open (usecode_use.md
    kegs: every door not magically locked within two tiles). The party waits on the beach; the Avatar alone drops
    the keg two tiles south of the gate, lights it from a step back, and walks off before the three-turn fuse ends"""
    p.loot_ok = False
    if not carried(p, 223): p.issue('gate', 'no powder keg'); return
    p.walk(787, 972)
    for _ in range(4): p.key('space', 'the party gathers')
    st = p.key('1', 'the Avatar alone'); p.log('  solo: %r' % st['msg'][-30:]); p.nav.refresh()
    p.walk(787, 966)
    if not drop_here(p, 223): p.issue('gate', 'could not drop the keg'); p.key('0'); return
    p.key('down'); st = p.act('u', 'up', 'light the keg'); p.log('  keg: %r' % st['msg'][-40:])
    for d in ('down', 'down', 'down'): p.key(d)
    for _ in range(4):
        st = p.key('space', 'the fuse'); 
        if 'blown' in st['msg'] or 'explo' in st['msg'].lower(): break
    p.log('  blast: %r at (%d,%d)' % (st['msg'][-80:], st['X'], st['Y']))
    p.key('0', 'party mode'); p.nav.refresh()
    if p.nav.route(*p.nav.pos(), {(787, 963)}) is None: p.issue('gate', 'the gate (787,964) still shut: %r' % st['msg'][-60:])

def seg_sutek(p, kw):
    """into Sutek's castle and down to the balloon plans. Past the blown gate a void (two rows) lies before the
    raised drawbridge, its cranks (785,959)/(789,959) on the far side: Telekinesis (Ort Por Ylem) works a crank
    from afar (magic_skills.md)"""
    p.loot_ok = False
    p.walk(789, 962); cast_light(p); p.nav.refresh()
    st = p.key('c', 'cast') if p.nav.route(*p.nav.pos(), {(787, 958)}) is None else p.g.state   # down already: a second turn raises it
    if st['cast']:
        p.g.type('opy'); st = p.g.state
        for _ in range(3): st = p.key('up', 'aim')
        st = p.key('ret', 'telekinesis'); p.idle(); p.log('  telekinesis on the crank: %r' % st['msg'][-60:])
    p.nav.refresh()
    if p.nav.route(*p.nav.pos(), {(787, 958)}) is None: p.issue('sutek', 'the drawbridge is still up: %r' % st['msg'][-60:]); return
    # the castle (Fox Fire FAQ's order, our objects): the entry's north secret door; one of the two fields; the
    # west hall's inner (east) wall hides a door; the fields before the levers; the left (q10) and middle (q9)
    # levers open all four portcullises (doorways 9 and 10) of the upper hall; Unlock Magic on the outer door;
    # its room's north wall hides a door; along the upper hall to the middle room (the hydra); its south wall
    # hides the door to the ladder (787,948)
    def search(x, y, d, what):
        p.walk(x, y); p.key('l', 'look'); st = p.at(d, 'search'); p.idle(); p.nav.refresh()
        p.log('  %s (%s of (%d,%d)): %r' % (what, d, x, y, st['msg'][-50:]))
    p.walk(787, 957)
    for _ in range(2):                                                   # a decorative sword and its mount hang on the hidden door: off the wall first
        st = p.act('g', 'up', 'take it down'); p.log('  get (787,956): %r' % st['msg'][-50:])
    search(787, 957, 'up', 'entry secret door')
    dispel_at(p, 786, 955, 'left')
    search(783, 953, 'right', 'west hall secret door')
    avatar_leads(p, 120)
    dispel_at(p, 785, 953, 'right')                                      # the corridor's own trap: sleep (786,788) and poison (787) fields on row 953
    dispel_at(p, 786, 953, 'right')                                      # (black pearls are few: the fire field at 785 is walked through)
    dispel_at(p, 787, 953, 'up')
    p.cure_up(); p.heal_up(0.5)
    p.walk(787, 952)
    for d in ('upleft', 'up'):
        st = p.act('u', d, 'lever'); p.log('  lever %s: %r' % (d, st['msg'][-50:]))
    p.nav.refresh(); p.walk(779, 951); avatar_leads(p); st = p.key('c', 'cast')
    if st['cast']:
        p.g.type('ep'); st = p.at('up', 'unlock magic'); p.idle(); p.log('  unlock magic (779,950): %r' % st['msg'][-50:])
        st = p.act('u', 'up', 'open'); p.log('  door: %r' % st['msg'][-40:]); p.nav.refresh()
    search(779, 947, 'up', 'outer room secret door')
    p.walk(787, 945)
    if p.foes(): p.fight()
    search(787, 945, 'down', 'middle room secret door')
    p.walk(787, 947)
    st = p.act('u', 'down', 'the ladder'); p.log('  ladder: L%d %r' % (st['L'], st['msg'][-40:])); p.nav.refresh()
    if st['L'] != 1: p.issue('sutek', 'not down Sutek\'s ladder: L%d (%d,%d)' % (st['L'], st['X'], st['Y']))

def seg_catacombs(p, kw):
    """Sutek's catacombs to the balloonist's body: level 1, the far-right wall switch (174 q104 at (189,224)) drops
    the electric field (doorway 104) south of the entrance ladder before the ladder down (195,251); level 2, the
    north wall's hidden door (195,248), a bear trap behind it (a damaging tile, walked over), on down; level 4,
    the body at (179,221) holds the balloon plans (270)"""
    p.loot_ok = False
    if p.g.state['L'] == 0 and not (770 <= p.nav.pos()[0] <= 805 and 935 <= p.nav.pos()[1] <= 980):   # recalled to the castle: back by skiff
        lb_heal(p)
        if min(count(p, 68), count(p, 71)) < 30 and p.g.state.get('gold', 0) >= 60: restock(p, 30)   # Heal's reagents ran out in the castle
        if not orb_go(p, 25): return
        if not board_skiff(p, (597, 118)): return                          # the deed fits both of Trebor's skiffs (quality 132)
        if not sail_to(p, {t for t in region(p, 787, 970) if abs(t[0] - 787) + abs(t[1] - 970) <= 20}): return
    if p.g.state['L'] == 0:
        p.sneak = True                                                   # past the moat's creatures into the entry: walk on
        try: p.walk(787, 957)
        finally: p.sneak = False
        p.walk(787, 947)                                                 # inside, what stands in the way is fought
        st = p.act('u', 'down', 'Sutek\'s ladder'); p.log('  ladder: L%d %r' % (st['L'], st['msg'][-40:])); p.nav.refresh()
    if p.g.state['L'] == 1:
        cast_light(p)
        if p.nav.route(*p.nav.pos(), {(195, 250)}) is None:              # the electric field (doorway 104) stands: the far-right wall switch
            p.walk(189, 225); st = p.act('u', 'up', 'switch'); p.log('  switch (189,224): %r' % st['msg'][-50:]); p.nav.refresh()
        p.walk(195, 250); st = p.act('u', 'down', 'ladder'); p.log('  ladder (195,251): L%d %r' % (st['L'], st['msg'][-40:])); p.nav.refresh()
    if p.g.state['L'] == 2:
        cast_light(p)
        if p.nav.route(*p.nav.pos(), {(195, 247)}) is None:
            p.walk(195, 249); p.key('l', 'look'); st = p.at('up', 'search'); p.idle(); p.nav.refresh()
            p.log('  hidden door (195,248): %r' % st['msg'][-50:])
        p.walk(203, 227, adj=True); st = use_way(p, {'obj': 305, 'x': 203, 'y': 227}); p.nav.refresh()   # the FAQ's way: right, then down
        p.log('  ladder (203,227): L%d' % p.g.state['L'])
    if not descend_to(p, 179, 221, 4): p.issue('catacombs', 'no way down to the body (179,221) L4 from L%d (%d,%d)' % (p.g.state['L'], *p.nav.pos())); return
    cast_light(p); p.heal_up(0.5)
    p.walk(179, 221, adj=True); px, py = p.nav.pos()
    p.key('l', 'look'); st = p.at(VDIR[(179 - px, 221 - py)], 'search the body'); p.idle(); p.log('  body: %r' % st['msg'][-140:])
    if not get_at(p, 179, 221, 270, tries=6): p.issue('catacombs', 'no balloon plans at (179,221) L4'); return

def seg_balloon(p, kw):
    """the balloon from its plans (270): out of the catacombs by Help; Michelle in Minoc weaves the basket (422,
    300 gold, she needs the plans); Arbeth in Paws spins 40 spider silk into a spool (226) once the town has
    spoken of him (Marissa, Thindle); Charlotte in New Magincia weaves the spool into silk cloth (241); Marissa
    sews the bag (421) from the cloth and the plans; Mortude in Paws sells rope (284); a cauldron (147) lies in
    the castle's cellar (108,82) level 1; U)se the plans: the balloon (420)"""
    p.loot_ok = False
    near = [o for o in p.g.objects()[0] if o['obj'] == 420]               # made already: it lies where the plans were used
    if carried(p, 420) or near:
        if near and not carried(p, 420): pick_heavy(p, near[0]['x'], near[0]['y'], 420)
        if not carried(p, 420): p.issue('balloon', 'no balloon in the pack')
        return
    if not carried(p, 270): p.issue('balloon', 'no balloon plans'); return
    if p.g.state['L'] >= 2:                                              # out of Sutek's catacombs by Help: the party back at the castle
        st = p.key('c', 'cast'); st = p.g.type('kl'); p.idle(); p.log('  help: %r at L%d (%d,%d)' % (st['msg'][-40:], st['L'], st['X'], st['Y']))
        p.nav.refresh()
    if count(p, 71) < 40 and not carried(p, 226) and not carried(p, 241) and not carried(p, 421):
        restock(p, 20)
        p.log('  spider silk: %d' % count(p, 71))
    if not carried(p, 422):                                              # the basket
        if not orb_go(p, 25): return
        wait_until(p, 7, 16, 'Michelle weaves 7-17')
        if not go_to_actor(p, 68, 'Michelle'): return
        lighten(p)
        out = p.talk_to(68, ['job', 'fath', 'plan', 'buy', 'y', '1', '3', 'y', 'y', 'to fly', 'bye'], 'Michelle', yes=True)
        p.log('  basket: %s (%r)' % (carried(p, 422), out and out[-3:]))
        if not carried(p, 422): p.issue('balloon', 'Michelle wove no basket'); return
    if not carried(p, 226) and not carried(p, 241) and not carried(p, 421):   # the silk: Paws
        if not orb_go(p, 3): return
        wait_until(p, 7, 14, 'Paws by day')
        if go_to_actor(p, 102, 'Marissa'): p.talk_to(102, ['arbe', 'bolt', 'ball', 'bye'], 'Marissa', yes=True)
        if go_to_actor(p, 100, 'Thindle'): p.talk_to(100, ['silk', 'ball', 'bye'], 'Thindle', yes=True)
        if not go_to_actor(p, 103, 'Arbeth'): return
        out = p.talk_to(103, ['timo', 'silk', 'y', '1', 'y', 'bye'], 'Arbeth', yes=True)
        p.log('  spool: %s (%r)' % (carried(p, 226), out and out[-3:]))
        if not carried(p, 226): p.issue('balloon', 'Arbeth spun no silk'); return
    if carried(p, 226) and not carried(p, 241):                          # the cloth: New Magincia
        if not orb_go(p, 11): return
        wait_until(p, 11, 14, 'Charlotte weaves 11-14')
        if not go_to_actor(p, 95, 'Charlotte'): return
        out = p.talk_to(95, ['silk', '1', 'y', 'buy', '1', 'y', 'bye'], 'Charlotte', yes=True)
        p.log('  cloth: %s (%r)' % (carried(p, 241), out and out[-3:]))
        if not carried(p, 241): p.issue('balloon', 'Charlotte wove no cloth'); return
    if carried(p, 241) and not carried(p, 421):                          # the bag: Marissa, 75 gold from the Avatar's purse
        equip_party(p)                                                   # (a wipe left the Avatar's gear in the pack)
        give_to(p, 88, 1)
        for _ in range(4):                                               # short of gold: Max buys a weapon
            if count(p, 88, 1) >= 75: break
            if not orb_go(p, 3) or not wait_for_work(p, 18, 'Max'): break
            out = p.talk_to(18, ['sell', '1', '1', 'y', 'bye'], 'Max'); p.log('  Max: %r, gold %d' % (out and out[-2:], count(p, 88, 1)))
        if not orb_go(p, 3): return
        h = p.g.state['min'] // 60
        wait_until(p, 15, 19, 'Marissa sews 15-20') if h >= 13 else wait_until(p, 6, 12, 'Marissa sews 6-13')
        if not go_to_actor(p, 102, 'Marissa'): return
        out = p.talk_to(102, ['ball', 'y', '1', 'y', 'bye'], 'Marissa', yes=True)
        p.log('  bag: %s (%r)' % (carried(p, 421), out and out[-3:]))
        if not carried(p, 421): p.issue('balloon', 'Marissa sewed no bag'); return
    if not carried(p, 284):                                              # the rope: Mortude
        if not orb_go(p, 3): return
        wait_until(p, 8, 17, 'Mortude by day')
        if go_to_actor(p, 101, 'Mortude'):
            out = p.talk_to(101, ['buy', 'rope', '1', 'y', 'bye'], 'Mortude', yes=True)
            p.log('  rope: %s (%r)' % (carried(p, 284), out and out[-3:]))
        if not carried(p, 284): p.issue('balloon', 'no rope from Mortude'); return
    if not carried(p, 147):                                              # the cauldron (108,82) level 1: the castle's cellar, past a door locked with key 1 (86,83)
        if p.g.state['L'] != 0: climb_out(p)
        if not orb_go(p, 8): return
        shed(p); lighten(p)                                              # in the light, together: the Avatar needs room for sixteen stones
        p.walk(291, 379, adj=True); use_way(p, {'obj': 305, 'x': 291, 'y': 379}); p.nav.refresh()
        cast_light(p)
        if p.g.state['L'] != 1: p.issue('balloon', 'the castle ladder (291,379) did not lead down'); return
        p.nav.unlock = lambda x, y, magic: unlock_any(p, x, y, magic)    # the cellar's rooms are locked too (key 1)
        try: p.walk(85, 83, keys=True)
        finally: p.nav.unlock = None
        if unlock_with_key(p, 86, 83):
            st = p.act('u', 'right', 'open'); p.log('  cellar door (86,83): %r' % st['msg'][-40:]); p.nav.refresh()
        if not beside(reach_set(p), 108, 82): p.issue('balloon', 'no way to the cauldron (108,82) L1 from (%d,%d)' % p.nav.pos()); return
        cast_light(p)
        if not get_at(p, 108, 82, 147):                                  # sixteen stones: Dupre, played alone, takes it (a thing got goes to the one played)
            party = p.g.state['party']; ids = [m['id'] for m in party]
            room = sorted((m.get('lim', 0) - m.get('wt', 0), m['id']) for m in party if m['id'] not in (1, 9) and m['hp'] > 0)
            if room and room[-1][0] >= 160:                                  # the member with the most room, played alone, takes it
                who = room[-1][1]
                st = p.key(str(ids.index(who) + 1), 'alone'); p.log('  solo %d (room %d): %r' % (who, room[-1][0], st['msg'][-30:])); p.nav.refresh()
                p.walk(108, 82, adj=True); px, py = p.nav.pos()
                st = p.act('g', VDIR[(108 - px, 82 - py)], 'takes it'); p.log('  %d gets: %r' % (who, st['msg'][-60:]))
                p.key('0', 'party mode'); p.nav.refresh()
            if not carried(p, 147): p.issue('balloon', 'no cauldron at (108,82) L1'); return
        climb_out(p)
    st = p.use_pack(270); p.log('  plans: %r' % (st and st['msg'][-60:]))   # "Done!": the balloon lies at the party's feet (usecode_use.md)
    x, y = p.nav.pos(); pick_heavy(p, x, y, 420)
    if not carried(p, 420): p.issue('balloon', 'no balloon in the pack')

def pick_heavy(p, x, y, obj_n):
    """G)et a heavy thing at (x,y) with the member who has the most room (load/limit in the report), played alone"""
    party = p.g.state['party']; ids = [m['id'] for m in party]
    room = sorted((m.get('lim', 0) - m.get('wt', 0), m['id']) for m in party if m['id'] != 9 and m['hp'] > 0)
    who = room[-1][1] if room else 1
    if who != 1: p.key(str(ids.index(who) + 1), 'alone'); p.nav.refresh(); p.log('  solo %d (room %d)' % (who, room[-1][0]))
    p.walk(x, y, adj=True); px, py = p.nav.pos()
    st = p.act('g', VDIR[(x - px, y - py)], 'get it'); p.log('  %d gets %d: %r' % (who, obj_n, st['msg'][-40:]))
    if who != 1: p.key('0', 'party mode'); p.nav.refresh()
    return carried(p, obj_n)

def wind_to(p, d):
    """C)ast Wind Change (Rel Hur): the wind goes the way asked (magic_skills.md)"""
    st = p.key('c', 'cast')
    if not st['cast']: p.idle(); return False
    p.g.type('rh'); st = p.key(d, 'the wind'); p.idle(); p.log('  wind %s: %r' % (d, st['msg'][-40:]))
    return True

def fly_to(p, tx, ty, near=2):
    """the balloon (wind.md §4): U)se it to inflate beside the party, U)se it to board; aboard it goes where the
    wind blows a tile a turn, Wind Change turns it; U)se to land beside"""
    if not p.g.state.get('veh'):
        st = p.use_pack(420); p.log('  balloon: %r' % (st and st['msg'][-40:]))
        b = [o for o in p.g.objects()[0] if o['obj'] == 423]
        if not b: p.issue('fly', 'no inflated balloon'); return False
        p.walk(b[0]['x'], b[0]['y'], adj=True); px, py = p.nav.pos()
        st = p.act('u', VDIR[(b[0]['x'] - px, b[0]['y'] - py)], 'board'); p.log('  board: %r veh=%s' % (st['msg'][-40:], st.get('veh')))
        if not p.g.state.get('veh'): p.issue('fly', 'could not board the balloon'); return False
    import collections
    for _ in range(12):                                                  # a route over the tiles it may fly (pass-map bit 0x20), in straight runs: a Wind Change each
        p.nav.refresh(); pm, S = p.nav.pm, p.nav.span; x0, y0 = p.nav.pos()
        if abs(x0 - tx) <= near and abs(y0 - ty) <= near: break
        came = {(x0, y0): None}; q = collections.deque([(x0, y0)]); end = None
        while q:
            x, y = q.popleft()
            if abs(x - tx) <= near and abs(y - ty) <= near: end = (x, y); break
            for dx, dy in ((0, -1), (0, 1), (1, 0), (-1, 0)):
                n = (x + dx, y + dy)
                if n not in came and 0 <= n[0] < S and 0 <= n[1] < S and pm[n[1] * S + n[0]] & 0x20: came[n] = (x, y); q.append(n)
        if end is None: p.issue('fly', 'no air route from (%d,%d) to (%d,%d)' % (x0, y0, tx, ty)); break
        path = []; c = end
        while c != (x0, y0): path.append(c); c = came[c]
        path.reverse()
        runs = []; px, py = x0, y0
        for nx, ny in path:
            d = VDIR[(nx - px, ny - py)]
            if runs and runs[-1][0] == d: runs[-1][1] += 1
            else: runs.append([d, 1])
            px, py = nx, ny
        p.log('  flight plan: %s' % runs)
        d, n = runs[0]
        wind_to(p, {'up': 'down', 'down': 'up', 'left': 'right', 'right': 'left'}[d])   # the wind blows FROM the way asked (magic_skills.md)
        for _ in range(n): p.key(d, 'fly')
    p.key('u'); st = p.key('ret', 'land'); p.nav.refresh(); p.log('  landed: %r at (%d,%d)' % (st['msg'][-40:], *p.nav.pos()))
    return not p.g.state.get('veh')

def seg_singularity(p, kw):
    """the Temple of Singularity (127,47) level 5, mountain-locked: the balloon from the Orb's gate (128,86) north.
    The Singularity asks for whom the Codex is sought: all; then the three catacombs' statues (Mondain, Minax,
    Exodus: their mantras UN, OR, US), and the temple again: the Mantra of Singularity, unorus"""
    p.loot_ok = False
    second = os.environ.get('SINGULARITY') == 'mantra'                    # back from the three catacombs
    near = [o for o in p.g.objects()[0] if o['obj'] == 420]
    if not carried(p, 420) and near: pick_heavy(p, near[0]['x'], near[0]['y'], 420)
    if not (p.g.state['L'] == 5 and abs(p.nav.pos()[1] - 48) < 6):
        if not carried(p, 420): p.issue('singularity', 'no balloon'); return
        if not orb_go(p, 17): return
        for _ in range(3):                                                  # each Wind Change costs mana: a camp first (no camp with foes in view)
            if p.g.state['party'][0].get('mp', 0) >= 40: break
            if p.g.state['foes']:                                          # gargoyles that keep their distance: walk out of their sight
                fs = p.foes(); x, y = p.nav.pos()
                if fs:
                    cx = sum(a['x'] for a in fs) / len(fs); cy = sum(a['y'] for a in fs) / len(fs)
                    far = sorted(((x + a, y + b) for a in range(-12, 13) for b in range(-12, 13) if p.nav.cost(x + a, y + b) is not None),
                                 key=lambda t: -((t[0] - cx) ** 2 + (t[1] - cy) ** 2))
                    for t in far[:5]:
                        try: p.nav.goto(*t, max_steps=80); break
                        except NavError: pass
                for _ in range(3): p.key('space', 'let them lose sight')
            m = p.rest_if_hurt(1.01)
            if p.g.state['party'][0].get('mp', 0) < 40 and 'wilderness' in p.g.state['msg'].lower():
                if not orb_go(p, 9): return                                    # walls near the gate count as town (rest.md §1): camp by Minax's gate
        if not orb_go(p, 17): return                                       # back to the balloon
        if not fly_to(p, 128, 49): return
    words = ['y', 'unorus', 'bye'] if second else ['sacr', 'all', 'bye']
    out = p.talk_at(127, 48, 200, words, 'the Singularity', yes=True)
    p.log('  singularity: %r' % (out and out[-3:]))
    near = [o for o in p.g.objects()[0] if o['obj'] == 420]             # the balloon lies where it landed: take it along (the temple is mountain-locked)
    if near and not carried(p, 420): pick_heavy(p, near[0]['x'], near[0]['y'], 420)

def open_barriers(p, tx, ty, tries=20):
    """a lever room (usecode_use.md levers): a lever or switch of quality Q toggles the barrier (portcullis 310,
    electric field 175) on every doorway (301) of quality Q. Until (tx,ty) can be walked to, pull one reachable
    actuator for each quality whose doorway has its barrier up (never one whose doorways stand open: that would
    raise a barrier). True when there"""
    pulled = set()
    for _ in range(tries):
        reach = reach_set(p)
        if beside(reach, tx, ty): return True
        objs = p.g.objects()[0]
        bars = {(o['x'], o['y']) for o in objs if o['obj'] in (310, 175)}
        shut = {o['q'] for o in objs if o['obj'] == 301 and (o['x'], o['y']) in bars and o['q']}
        open_ = {o['q'] for o in objs if o['obj'] == 301 and (o['x'], o['y']) not in bars and o['q']}
        acts = [o for o in objs if o['obj'] in (268, 174) and o['q'] in shut - open_ and o['q'] not in pulled and beside(reach, o['x'], o['y'])]
        if not acts: p.log('  barriers: nothing to pull (shut %s, open %s)' % (sorted(shut), sorted(open_))); return False
        a = acts[0]; pulled.add(a['q'])
        p.walk(a['x'], a['y'], adj=True); px, py = p.nav.pos()
        st = p.act('u', VDIR[(a['x'] - px, a['y'] - py)], 'pull'); p.log('  pull %s q%d (%d,%d): %r' % ('lever' if a['obj'] == 268 else 'switch', a['q'], a['x'], a['y'], st['msg'][-40:]))
        p.nav.refresh()
    return beside(reach_set(p), tx, ty)

STATUES = [(7, (67, 42), 397, (67, 11, 4), 189, 'cont', 'un', 'Mondain'), (9, (188, 42), 398, (187, 10, 4), 190, 'pass', 'or', 'Minax'),
           (18, (108, 218), 399, (83, 219, 3), 191, 'dili', 'us', 'Exodus')]   # Orb position, the catacomb's cave (level 5 -> 4), the statue, its script, its topic, the mantra

def seg_statues(p, kw):
    """the catacombs of Control, Passion and Diligence: each statue (Mondain, Minax, Exodus) teaches its principle
    and has the Avatar repeat its mantra (UN, OR, US)"""
    p.loot_ok = False
    def ready_up():                                                      # the recall leaves the Avatar's gear in the pack (combat.md §3d)
        if carried(p, 141): drop_here(p, 141)                            # (the decorative sword from Sutek's wall is no weapon)
        equip_party(p)
        if count(p, 43, 1): ready(p, 43)
    lb_heal(p); ready_up()
    if min(count(p, 68), count(p, 71)) < 25 and p.g.state.get('gold', 0) >= 40: restock(p, 25)   # Heal's ginseng and silk
    skip = set(os.environ.get('SKIP_STATUES', '').split(','))             # those already taught (a rerun)
    for pos, cave, obj, (sx, sy, sz), tid, topic, mantra, name in STATUES:
        if name in skip: continue
        if any(m['hp'] < m['max'] * 0.8 or m.get('psn') for m in p.g.state['party'] if m['id'] != 9) or (p.g.state['L'] == 0 and abs(p.g.state['X'] - 307) < 20):
            lb_heal(p); ready_up()                                       # no reagents for Heal: Lord British, before each catacomb
        if not orb_go(p, pos): return
        if p.g.state['party'][0].get('mp', 0) < 20: p.rest_if_hurt(1.01)    # Light and Mass Invisibility need mana: a camp restores it (Lord British doesn't)
        p.walk(*cave); p.nav.refresh(); cast_light(p)
        st = p.g.state; p.log('  %s cave (%d,%d): L%d (%d,%d)' % (name, cave[0], cave[1], st['L'], st['X'], st['Y']))
        if p.g.state['L'] == sz and not beside(reach_set(p), sx, sy): open_barriers(p, sx, sy)   # the catacomb's lever room
        if name == 'Exodus' and p.g.state['L'] == 4:                     # a maze of locked doors to a hidden ladder (103,239) up
            p.nav.unlock = lambda x, y, magic: unlock_any(p, x, y, magic)
            try: p.walk(103, 236, keys=True)
            finally: p.nav.unlock = None
            p.key('l', 'look'); st = p.at('down', 'search'); p.idle(); p.nav.refresh(); p.log('  hidden door (103,237): %r' % st['msg'][-40:])
            p.walk(103, 238); st = p.act('u', 'down', 'ladder'); p.nav.refresh(); p.log('  ladder (103,239): L%d' % p.g.state['L'])
        if name == 'Exodus' and p.g.state['L'] == 3:                     # daemons on his level: unseen (Mass Invisibility), straight to him
            avatar_leads(p); st = p.key('c', 'cast')
            if st['cast']:
                p.g.type('vsl'); st = p.g.state
                if st.get('verb') == 'C': st = p.key('ret', 'on the party')   # an area spell: its centre is the Avatar's own tile
                p.idle(); p.log('  mass invisibility: %r' % st['msg'][-50:])
            p.sneak = True
        if name == 'Exodus' and p.g.state['L'] == 3 and not beside(reach_set(p), sx, sy): dispel_at(p, 83, 216, 'down')   # fields ring him
        if p.g.state['L'] != sz and not ascend_to(p, sx, sy, sz): p.issue('statues', 'no way up to %s (%d,%d) L%d' % (name, sx, sy, sz)); continue
        cast_light(p)
        if not beside(reach_set(p), sx, sy): open_barriers(p, sx, sy)
        if not beside(reach_set(p), sx, sy): p.issue('statues', 'no way to %s (%d,%d) L%d' % (name, sx, sy, sz)); continue
        p.sneak = name in ('Minax', 'Exodus')                            # daemons guard them: straight there, a few words, away
        try: out = p.talk_at(sx, sy, tid, ['task', topic, mantra, 'bye'], name, yes=True)   # the topic ends "Repeat after me the Mantra": the mantra
        finally: p.sneak = False
        p.log('  %s: %r' % (name, out and out[-2:]))

def count(p, obj_n, own=None):
    """how many of obj_n the party carries (own: only that member's own; a spell takes the caster's reagents)"""
    return sum(r['qty'] or 1 for r in p.g.objects()[1] if r['obj'] == obj_n and (own is None or r.get('own', 1) == own))

def restock(p, n=6):
    """Rudyom in Cove (by the Compassion shrine) sells reagents in shop hours: ginseng, spider silk and garlic
    for Heal and Dispel Magic. Each member pays from their own purse, so each is asked in turn"""
    if not orb_go(p, 4): return
    p.rest_if_hurt(0.8)
    wait_until(p, 9, 16, 'shop hours')
    if not go_to_actor(p, 128, 'Rudyom'): return
    before = (count(p, 68), count(p, 71), count(p, 67))
    for item, obj in ((3, 68), (5, 71), (2, 67)):                          # ginseng, spider silk, garlic (his list)
        for m in range(1, len(p.g.state['party']) + 1):                    # whoever can pay
            c0 = count(p, obj)
            p.talk_to(128, ['buy', str(m), str(item), str(n)], 'Rudyom')
            if count(p, obj) > c0: break
    for item in (8, 7, 6, 4, 1):                                          # sulfurous ash for Light (In Lor): where it is in his list
        if count(p, 72) >= n: break
        c0 = count(p, 72); p.talk_to(128, ['buy', '1', str(item), str(n)], 'Rudyom')
        if count(p, 72) > c0: p.log('  Rudyom: ash is item %d' % item); break
    for obj in (68, 71, 67, 72):                                          # the caster's own: they go to the Avatar
        for _ in range(4):
            if count(p, obj) == count(p, obj, 1) or not give_to_avatar(p, obj, others=True): break
    p.log('  restock: ginseng/silk/garlic %s -> %s, gold %s' % (before, (count(p, 68), count(p, 71), count(p, 67)), p.g.state.get('gold')))

KEEP = {57, 60, 62, 63, 64, 73, 87, 88, 90, 128, 129, 130, 131, 132, 133, 209, 210, 258, 265, 275, 394, 395, 396} | set(range(65, 73)) | set(range(242, 250))
def drop_junk(p):
    """leave behind what the fights brought (spare weapons and armour, mugs, lutes): the party's load has a
    limit and a moonstone must still fit. D)rop: D, D through the pack to it, Enter, Enter (at the feet)"""
    if p.g.state['L'] != 0: return
    for _ in range(40):
        inv = p.g.objects()[1]
        junk = [r for r in inv if r['obj'] not in KEEP and (r['obj'] < 57 or r['obj'] in (89, 114, 115, 116, 117, 118, 119, 120, 153, 158, 188, 191))]
        if not junk: return
        name = junk[0].get('name', '')
        p.idle(); st = p.key('d', 'drop')
        for _ in range(len(inv) + 2):
            st = p.key('d', 'next')
            if ('Drop-' + name) in st['msg']: break
        else: p.idle(); return
        p.key('ret'); st = p.key('ret', 'at the feet')
        if 'How many' in st['msg'] or 'Location' in st['msg']: st = p.key('ret', 'all of it')
        p.log('  drop %s: %r' % (name, st['msg'][-50:])); p.idle()
        if any(r['obj'] == junk[0]['obj'] for r in p.g.objects()[1]) and len([r for r in p.g.objects()[1] if r['obj'] == junk[0]['obj']]) >= len([r for r in inv if r['obj'] == junk[0]['obj']]):
            p.idle(); return

def lb_heal(p):
    """Lord British heals the whole party when asked ("any time thou dost need healing, do thou but ask me")"""
    if not orb_go(p, 8): return
    if p.foes(): p.fight()                                              # the throne room's egg (eggs.md)
    p.talk_to(5, ['heal'], 'Lord British')
    p.log('  Lord British: party %s' % ' '.join('%d:%d/%d' % (m['id'], m['hp'], m['max']) for m in p.g.state['party']))

def equip_party(p):
    """ready what the pack holds to fight with (after the Avatar's death his readied things are in the pack,
    combat.md §3d; a fallen companion's stay on the body): each weapon or armour, and the invisibility ring,
    on the first member who takes it"""
    n = len(p.g.state['party'])
    for _ in range(30):
        inv = p.g.objects()[1]
        gear = [r for r in inv if (r['obj'] < 57 and r['obj'] not in (33, 56, 55)) or r['obj'] == 258]   # not slings and ammunition
        gear = [r for r in gear if (r['obj'], r.get('name')) not in getattr(p, 'unfit', set())]
        if not gear: return
        r = gear[0]; got = False
        for m in range(1, n + 1):
            if ready(p, r['obj'], m): got = True; break
            if sum(1 for q in p.g.objects()[1] if q['obj'] == r['obj']) < sum(1 for q in inv if q['obj'] == r['obj']): got = True; break
        if not got: p.unfit = getattr(p, 'unfit', set()) | {(r['obj'], r.get('name'))}

def keep_up(p):
    """between chapters: rest off wounds where it may, buy reagents when Heal is about to run dry, and
    go to Lord British when the party is hurt and Heal can't mend it"""
    p.rest_if_hurt(0.6)
    equip_party(p)
    drop_junk(p)
    gold = p.g.state.get('gold', 0)
    if min(count(p, 68), count(p, 71)) < 6 and gold >= 60: restock(p, 12 if gold >= 200 else 6)   # Heal, Dispel Magic and Create Food all take ginseng
    if any(m['max'] and m['hp'] < m['max'] * 0.6 for m in p.g.state['party']) and min(count(p, 68, 1), count(p, 71, 1)) < 1: lb_heal(p)
    if p.poisoned() and min(count(p, 67, 1), count(p, 68, 1)) < 1: lb_heal(p)

def norm(name):
    """a merchant's item name and a look name compared alike: case, armour/armor, a leading article"""
    n = name.lower().replace('armour', 'armor').strip()
    for a in ('the ', 'a ', 'an '):
        if n.startswith(a): n = n[len(a):]
    return n

def fill_cube(p, who=3):
    """The moonstones go INTO the vortex cube (usecode_use.md cube; equipment.md, containers in the pack: M)ove a
    moonstone, "To ", the cube). All eight and then the cube go to one lightly laden member, so the cube is the
    newest thing, the first cell of his pack, and the moonstones sit on the same page; then per moonstone:
    M, M through the pack to its row, Enter, Tab (the crosshair on the first cell), Enter"""
    for _ in range(4):
        give_to(p, 73, who)
        if all(r.get('own') == who for r in p.g.objects()[1] if r['obj'] == 73): break
    give_to(p, 62, who)
    for _ in range(12):
        inv = p.g.objects()[1]; cube = [r for r in inv if r['obj'] == 62]
        if not cube or cube[0].get('own') != who: p.issue('ending', 'the cube is not with member %d' % who); return False
        cid = cube[0].get('id', 0)
        loose = [r for r in inv if r['obj'] == 73 and (not cid or r.get('in', 0) != cid)]
        if not loose: return True
        p.idle(); p.key('m', 'move'); st = p.key('m', 'the pack')
        for _ in range(loose[0]['row']): st = p.key('m', 'next')
        if not st['msg'].startswith('Move the moonstone'): p.issue('ending', 'the pack prompt: %r' % st['msg'][-40:]); p.idle(); return False
        p.key('ret', 'this one'); p.key('tab', 'the panel'); st = p.key('ret', 'the first cell')
        p.log('  moonstone %d: %r' % (loose[0]['fr'], st['msg'][-40:])); p.idle()
    return False

def seg_ending(p, kw):
    """The Codex (usecode_use.md vortex cube): the Orb to the shrine's isle (19), the Britannian lens down at
    (921,851) and the gargoyle lens at (925,851), then U)se the vortex cube with all eight moonstones carried:
    "The Codex has vanished!" and the ending plays"""
    for obj_n, what in ((62, 'vortex cube'), (394, 'Britannian lens'), (396, 'gargoyle lens')):
        if not carried(p, obj_n): p.issue('ending', 'no %s' % what); return
    phases = sorted({r['fr'] for r in p.g.objects()[1] if r['obj'] == 73})
    if phases != list(range(8)): p.issue('ending', 'moonstone phases carried: %r' % phases); return
    if not fill_cube(p): p.issue('ending', 'the moonstones are not all in the cube'); return
    if not orb_go(p, 19): return
    avatar_leads(p)
    for (x, y), obj_n in (((921, 851), 394), ((925, 851), 396)):
        try: p.walk(x, y)
        except NavError as e: p.issue('ending', 'to the lens tile (%d,%d): %s' % (x, y, e)); return
        if not drop_here(p, obj_n): p.issue('ending', 'lens %d not set down at (%d,%d)' % (obj_n, x, y)); return
    p.walk(923, 853)
    st = p.use_pack(62, 'cube')
    for _ in range(60):
        if st is None or 'vanished' in st['msg'].lower(): break
        st = p.key('space', 'the cube')
    msg = (st or p.g.state)['msg']; p.log('  the cube: %r' % msg[-120:]); p.g.shot('ending')
    if 'vanished' not in msg.lower(): p.issue('ending', 'the cube did not take the Codex: %r' % msg[-80:])

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
        out = p.talk_to(18, ['sell', '1', pick, 'y'], 'Max')          # which of you: 1 (the Avatar holds the pooled pack, as seg_sell)
        if out is None: p.issue('trade', 'could not reach Max'); return
        text = ' '.join(r for _, r in out)
        m = re.search(r'take (\d+) gold for that ([^,?]+)', text)
        if m and not any(w == 'y' for w, _ in out): m = None              # an offer it did not take is no sale
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
    if member(p, 4): return
    p.expect_body = 4
    try: seg_healer(p, kw)
    finally: p.expect_body = None
    m4 = member(p, 4)
    p.log('  paid resurrection: gold %d -> %d, Iolo %d/%d, on the map %s' % (g0, p.g.state['gold'], m4['hp'], m4['max'], bool(p.actor(4))))
    if m4['hp'] == 0: p.issue('paid', 'Iolo still dead after paying')
    p.meant.clear()

def seg_book(p, kw):
    """a new game's magic with the real rules (magic_skills.md §4, §6): no book, then the castle's book off
    its table, readied, its spells cast, kept over a save and a load. Run alone: main leaves MAGIC.REQ out"""
    if p.g.state['foes'] and p.foes(): p.fight()                       # the opening's gargoyles first
    st = p.g.state
    if st['spells']: p.issue('book', 'a new game knows %d spells' % st['spells'])
    st = p.key('c', 'cast')
    if st['cast'] or 'No spellbook is readied' not in st['msg']: p.issue('book', 'C with no book: cast=%s %r' % (st['cast'], st['msg'])); p.idle()
    st = p.walk(300, 367, adj=True)
    d = (300 - st['X'], 367 - st['Y'])
    p.g.shot('table.png')
    st = p.act('g', VDIR[d], 'get the book')
    p.log('  got: %r, spells %d' % (st['msg'], st['spells']))
    if st['spells'] != 7: p.issue('book', 'the book gave %d spells, not 7 (%r)' % (st['spells'], st['msg']))
    st = p.key('c', 'cast')
    if st['cast'] or 'No spellbook is readied' not in st['msg']: p.issue('book', 'C with the book in the pack: cast=%s %r' % (st['cast'], st['msg'])); p.idle()
    st = p.key('e', 'equip')
    for _ in range(40):
        if 'spell' in st['msg'].lower(): break
        st = p.key('e', 'next')
    p.key('1', 'ready it'); p.idle()
    st = p.g.state
    if not st['book']: p.issue('book', 'the book is not readied (%r)' % st['msg'])
    st = p.key('c', 'cast')
    if not st['cast']: p.issue('book', 'C with the book readied: %r' % st['msg']); p.idle(); return
    st = p.g.type('vfh')                                               # Explosion: not in this book
    p.log('  explosion: %r' % st['msg'])
    if 'nothing' not in st['msg']: p.issue('book', 'a spell not in the book: %r' % st['msg'])
    p.key('c', 'cast'); st = p.g.type('im')                            # Heal: needs reagents nobody holds
    p.log('  heal: %r' % st['msg'])
    if 'No Reagents' not in st['msg']: p.issue('book', 'Heal with no reagents: %r' % st['msg'])
    p.key('s', 'the slot page'); st = p.key('ret', 'save'); p.log('  save: %r' % st['msg'])
    if st['verb'] == 'Q' or 'saved' not in st['msg'].lower(): p.issue('book', 'save: %r' % st['msg'])
    p.g.shot('book.png')

def seg_parchment(p, kw):
    """the real rules of a bought spell (magic_skills.md §4): it waits in the pack until M M Enter puts it in the
    book, a known one stays out, and a parchment outlasts a save and a load. Run alone with PARCH.REQ = Xiao"""
    st = p.walk(300, 367, adj=True); d = (300 - st['X'], 367 - st['Y'])
    st = p.act('g', VDIR[d], 'get the book'); before = st['spells']
    st = p.key('e', 'equip')
    for _ in range(40):
        if 'spell' in st['msg'].lower(): break
        st = p.key('e', 'next')
    p.key('1', 'ready it'); p.idle()
    out = p.talk_to(36, ['spells', '1', '3', 'y', 'spells', '2', '1', 'y', 'bye'], name='Xiao', yes=True)
    for w, r in out or []: p.log('  %r -> %r' % (w, r[-200:]))
    st = p.g.state; p.log('  bought: parch %d spells %d gold %s' % (st['parch'], st['spells'], st.get('gold')))
    if st['parch'] < 2: p.issue('parchment', 'bought 2 parchments, the pack holds %d' % st['parch']); return
    if st['spells'] != before: p.issue('parchment', 'a parchment joined the book by itself: %d -> %d' % (before, st['spells']))
    def into_book(skip=0):
        p.key('m', 'move'); st = p.key('m', 'the pack')
        for _ in range(40):
            if 'Move the spell?' in st['msg']: break
            st = p.key('m', 'next')
        for _ in range(skip): st = p.key('m', 'the next spell')
        time.sleep(0.5); p.g.shot('movepack.png')
        return p.key('ret', 'into the book')
    st = into_book(); p.log('  Light: %r spells %d parch %d' % (st['msg'], st['spells'], st['parch']))     # the castle's book has it
    if 'already has' not in st['msg'] or st['spells'] != before or st['parch'] != 2: p.issue('parchment', 'a known spell: %r spells %d parch %d' % (st['msg'], st['spells'], st['parch']))
    st = into_book(1); p.log('  Infravision: %r spells %d parch %d' % (st['msg'], st['spells'], st['parch']))
    if st['spells'] != before + 1 or st['parch'] != 1: p.issue('parchment', 'moved in: spells %d parch %d %r' % (st['spells'], st['parch'], st['msg']))
    p.key('s', 'the slot page'); st = p.key('ret', 'save'); p.log('  save: %r' % st['msg'])
    p.g.key('r', ctrl=True); st = p.key('ret', 'restore'); p.log('  restore: %r spells %d parch %d book %d' % (st['msg'], st['spells'], st['parch'], st['book']))
    if st['spells'] != before + 1 or st['parch'] != 1 or not st['book']: p.issue('parchment', 'after a load: spells %d parch %d book %d' % (st['spells'], st['parch'], st['book']))
    st = into_book(); p.log('  Light again: %r spells %d parch %d' % (st['msg'], st['spells'], st['parch']))
    if 'already has' not in st['msg'] or st['parch'] != 1: p.issue('parchment', 'a known spell after the load: %r parch %d' % (st['msg'], st['parch']))
    st = p.key('m', 'move'); st = p.key('esc', 'cancel')
    if st['verb']: p.issue('parchment', 'Esc left verb %r' % st['verb'])

def seg_look(p, kw):
    """L)ook's crosshair with real keys: the bare ground, Esc, a walked cursor, a person's portrait"""
    st = p.key('l', 'look'); time.sleep(0.3); p.g.shot('look_cursor.png')
    if st['verb'] != ord('L') and st['verb'] != 'L': p.log('  verb %r' % st['verb'])
    st = p.key('right', 'cursor'); st = p.key('right', 'cursor'); time.sleep(0.3); p.g.shot('look_walk.png')
    st = p.key('ret', 'look there'); p.log('  far: %r' % st['msg']); time.sleep(0.3); p.g.shot('look_far.png')
    if 'Thou dost see' not in st['msg']: p.issue('look', 'no look line: %r' % st['msg'])
    if 'Searching' in st['msg']: p.issue('look', 'searched at two tiles: %r' % st['msg'])
    st = p.key('l', 'look'); st = p.key('esc', 'cancel')
    if st['verb']: p.issue('look', 'Esc left verb %r' % st['verb'])
    st = p.key('g', 'get'); st = p.key('right', 'get east'); p.log('  get: %r' % st['msg'])   # a direction acts at once (usecode_use.md)
    if 'Get-' not in st['msg']: p.issue('look', 'get echo: %r' % st['msg'])
    st = p.key('u', 'use'); st = p.key('ret', 'use here'); p.log('  use: %r' % st['msg'])
    x, y = p.nav.pos(); near = None
    for a in p.g.actors():
        if a['id'] not in [m for m in st.get('party', [])] and abs(a['x'] - x) <= 5 and abs(a['y'] - y) <= 5 and a['id'] > 1: near = a; break
    p.log('  someone near: %r' % (near,))
    if near:
        p.key('l', 'look')
        for _ in range(abs(near['x'] - x)): p.key('right' if near['x'] > x else 'left')
        for _ in range(abs(near['y'] - y)): p.key('down' if near['y'] > y else 'up')
        st = p.key('ret', 'look at them'); p.log('  portrait: %r' % st['msg'])

def seg_panel(p, kw):
    """the status display (equipment.md §5): inventory, portrait, keys; a shot of each"""
    st = p.key('i', 'inventory'); time.sleep(0.3); p.g.shot('inv.png'); p.log('  inv %r' % st.get('inv'))
    st = p.key('asterisk' if 0 else 'tab', 'tab'); time.sleep(0.2); p.g.shot('inv_tab.png')
    st = p.key('tab', 'tab back'); st = p.key('f10', 'leave'); time.sleep(0.2)
    if st.get('inv'): p.issue('panel', 'F10 left the inventory open')
    st = p.key('tab', 'tab opens'); st = p.key('down'); st = p.key('down'); st = p.key('ret', 'select'); p.log('  selected: %r' % st['msg'])
    st = p.key('tab', 'done'); time.sleep(0.2); p.g.shot('inv_ready.png'); st = p.key('f10', 'leave')
    x0, y0 = p.nav.pos()
    st = p.g.char('2'); p.check('solo'); p.log('  solo: %r at (%d,%d)' % (st['msg'], st['X'], st['Y'])); time.sleep(0.3); p.g.shot('solo.png')
    if (st['X'], st['Y']) == (x0, y0): p.issue('panel', 'Solo did not move the view')
    st = p.key('down', 'solo step'); st = p.key('down', 'solo step'); p.log('  solo walked to (%d,%d)' % (st['X'], st['Y']))
    p.key('t'); p.key('right'); st = p.key('ret', 'talk in solo'); p.log('  talk: %r' % st['msg'])
    if 'solo' not in st['msg'] and 'No-one' not in st['msg']: p.issue('panel', 'talk in solo: %r' % st['msg'])
    st = p.g.char('0'); p.check('party'); p.log('  party: %r at (%d,%d)' % (st['msg'], st['X'], st['Y']))
    if (st['X'], st['Y']) != (x0, y0): p.issue('panel', 'Party mode did not come back: (%d,%d)' % (st['X'], st['Y']))
    time.sleep(0.3); p.g.shot('party.png')
    st = p.key('s', 'save page'); st = p.key('ret', 'save'); p.log('  save %r' % st['msg'])

def seg_usekit(p, kw):
    """U)se in two steps (usecode_use.md): needs USEKIT.REQ in the drive folder"""
    st = p.use_pack(63, 'lock'); p.log('  pick asks: %r verb %r' % (st['msg'], st.get('pend')))
    st = p.key('right', 'crosshair'); st = p.key('ret', 'on the door'); p.log('  pick: %r' % st['msg'])
    if 'door' not in st['msg']: p.issue('usekit', 'pick: %r' % st['msg'])
    st = p.use_pack(103, 'pick'); p.log('  dig: %r' % st['msg'])
    if 'Not readied' not in st['msg']: p.issue('usekit', 'dig: %r' % st['msg'])
    st = p.use_pack(154, 'telescope'); st = p.key('right', 'east'); p.log('  telescope: %r' % st['msg'])
    if 'Done' not in st['msg']: p.issue('usekit', 'telescope: %r' % st['msg'])
    st = p.use_pack(154, 'telescope'); st = p.key('f1' if 0 else 'ret', 'nowhere'); p.log('  telescope: %r' % st['msg'])
    if 'nowhere' not in st['msg']: p.issue('usekit', 'telescope nowhere: %r' % st['msg'])
    st = p.act('u', 'left', 'fountain'); p.log('  fountain: %r' % st['msg'])
    st = p.g.char('y'); p.log('  wish: %r' % st['msg'])
    st = p.g.type('Food'); p.log('  wished: %r' % st['msg'])
    if 'Failed' not in st['msg'] and 'food' not in st['msg']: p.issue('usekit', 'wish: %r' % st['msg'])
    st = p.act('u', 'left'); st = p.g.char('n'); p.log('  no wish: %r' % st['msg'])
    st = p.use_pack(155, 'crystal'); p.log('  ball: %r' % st['msg'])
    if 'degrees' in st['msg']:
        st = p.g.type('200n'); p.log('  ball bad: %r' % st['msg'])
    st = p.use_pack(97, 'gargish') or p.use_pack(97); p.log('  vocabulary: %r' % st['msg'])
    st = p.key('d')
    for _ in range(80):
        if 'bread' in st['msg']: break
        st = p.key('d')
    st = p.key('down'); st = p.key('ret', 'where'); p.log('  how many: %r' % st['msg'])
    st = p.g.type('2'); p.log('  dropped: %r' % st['msg'])
    if 'bread' not in st['msg']: p.issue('usekit', 'drop: %r' % st['msg'])

LEVEL_XP = (0, 100, 200, 400, 800, 1600, 3200, 6400)   # the shrine scripts' table, by current level (combat.md §3)
def party_line(p):
    return ' '.join('%d:L%d %d/%d xp%d' % (m['id'], m['lvl'], m['hp'], m['max'], m.get('exp', 0)) for m in p.g.state['party'])

def can_level(m):
    return 0 < m['lvl'] < 8 and m['hp'] > 0 and m.get('exp', 0) >= LEVEL_XP[m['lvl']]

def meditate(p, v=1):
    """every member with the experience for it meditates at shrine v (any shrine levels; each adds its own
    gift): T)alk to the shrine, y, the member's number, the mantra (usecode_vm.md §6c)"""
    _, (fx, fy), lv, pos, mantra = SHRINES[v]
    if not any(can_level(m) for m in p.g.state['party']): return 0
    if not orb_go(p, pos): return 0
    if p.foes(): p.fight(); p.heal_up()
    gained = 0
    for _ in range(len(p.g.state['party'])):
        ids = [m['id'] for m in p.g.state['party']]
        due = [i + 1 for i, m in enumerate(p.g.state['party']) if can_level(m)]
        if not due: break
        p.walk(fx, fy, adj=True); lv0 = {m['id']: m['lvl'] for m in p.g.state['party']}
        st = talk_object(p, fx, fy); out = [('', st['conv'])]
        if not st['talk']: p.issue('train', 'T)alk to shrine %d: %r' % (v, st['msg'])); break
        for _ in range(8):
            st = p.g.state
            if not st['ask']: break
            a = 'y' if st.get('askc') else (str(due[0]) if st.get('num') else mantra)
            st = p.say(a); out.append((a, st['conv']))
        p.idle(); p.transcripts.setdefault('meditate%d' % v, []).append(out)
        lv1 = {m['id']: m['lvl'] for m in p.g.state['party']}
        up = [i for i in lv1 if lv1[i] > lv0.get(i, 99)]
        p.log('  meditate at shrine %d, member %d (%d): %s' % (v, due[0], ids[due[0] - 1], party_line(p)))
        if not up: p.issue('train', 'member %d had the experience but gained no level at shrine %d: %r' % (ids[due[0] - 1], v, ' '.join(r for _, r in out)[-300:])); break
        gained += len(up)
    return gained

# where evil things hatch over and over on the surface (eggs.md §1-§2; the new-game egg list read with objfind):
# a hatch needs the party within 20 tiles but farther than 8, and the egg re-arms beyond 20
GRIND = [(913, 504), (283, 115), (178, 283), (164, 355)]   # a skeleton by gate 1, trolls by gate 15, headless and trolls west of the castle
# left out after trying: (521,327) poisoned the Avatar each visit, (131,460) mongbats kill at level 3, (569,340) and
# (936,824) hatch out of walking reach, (424,934) has no way near

GRIND_SKIP = set(); SITE_GATE = {}
EGG_XP = {352: 12, 355: 13, 357: 16, 361: 9, 366: 20, 368: 14, 370: 17, 371: 16, 378: 19, 424: 29, 426: 9, 427: 8}   # combat.md §3: (str+dex+int+hp+dmg+xp)/4
EGG_AVOID = {362, 363, 367, 369, 372, 411, 413}         # gargoyles, daemons, drakes, mongbats, dragons, serpents: too strong for now
def hunt_sites(p, reach=90):
    """surface eggs of creatures worth experience, reachable on foot within `reach` steps of an Orb gate:
    [(value, x, y, gate)], best first. Eggs from the new-game data (objfind), the walk from the game's pass map"""
    import subprocess, collections
    exe = os.path.join(ROOT, 'build', 'objfind')
    if not os.path.exists(exe): subprocess.run(['make', '-C', ROOT, 'objfind'], check=True, capture_output=True)
    out = subprocess.run([exe, REF, '335'] + [str(o) for o in EGG_XP], check=True, capture_output=True, text=True).stdout
    eggs, emb = {}, collections.defaultdict(list)
    for f in (l.split() for l in out.splitlines()):
        o, q, qty, x, y, z, c = (int(f[i]) for i in (0, 2, 3, 4, 5, 6, 7))
        if z: continue
        if o == 335: eggs[(x, y)] = (q, qty)
        elif c == 335: emb[(x, y)].append((o, qty))
    out2 = subprocess.run([exe, REF] + [str(o) for o in EGG_AVOID], check=True, capture_output=True, text=True).stdout
    bad = {(int(f[4]), int(f[5])) for f in (l.split() for l in out2.splitlines()) if f[7] == '335' and f[6] == '0'}
    p.nav.refresh(); S = p.nav.span
    dist = {}; dq = collections.deque()
    for k, (gx, gy) in ORB.items():
        if ORB_LEVEL.get(k, 0) == 0: dist[(gx, gy)] = (0, k); dq.append((gx, gy))
    while dq:                                            # one walk from every gate at once
        x, y = dq.popleft(); d, k = dist[(x, y)]
        if d >= reach: continue
        for a, b in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            t = ((x + a) % S, (y + b) % S)
            if t not in dist and p.nav.cost(*t) is not None: dist[t] = (d + 1, k); dq.append(t)
    sites = []
    for (x, y), e in emb.items():
        q, ch = eggs.get((x, y), (0, 0))
        if (x, y) in bad or q % 10 in (1, 3) or not ch: continue          # neutral or good hatchlings are no foes
        v = sum(EGG_XP.get(o, 0) * (n + 1) / 2 for o, n in e) * min(ch, 100) / 100
        near = [dist[(x + a, y + b)] for a in (-1, 0, 1) for b in (-1, 0, 1) if (x + a, y + b) in dist]
        if v >= 8 and near: d, k = min(near); sites.append((round(v), x, y, k, d))
    sites.sort(key=lambda t: -t[0] / (30 + t[4]))
    p.log('  hunting sites: %s' % sites[:12])
    SITE_GATE.update({(x, y): k for _, x, y, k, _ in sites})
    return [(x, y) for _, x, y, _, _ in sites[:8]]

def ring_spot(p, ex, ey, r0, r1):
    """a reachable tile whose distance (the larger axis) to (ex,ey) is r0..r1, the nearest first"""
    p.nav.refresh(); x, y = p.nav.pos(); S = p.nav.span
    cand = [((ex + a) % S, (ey + b) % S) for a in range(-r1, r1 + 1) for b in range(-r1, r1 + 1) if r0 <= max(abs(a), abs(b))]
    cand.sort(key=lambda t: max(abs(t[0] - x), abs(t[1] - y)))
    for t in cand[:400]:
        if p.nav.cost(*t) is not None and p.nav.route(x, y, {t}, limit=60000) is not None: return t
    return None

ROSTER = set()                                                         # who trains (seg_train): the fallen are raised

def raise_dead(p):
    """a companion fell: C)ast Help (Kal Lor, no reagents), Lord British raises the dead with their gear and
    recalls the party to the castle (combat.md §3d, the recall)"""
    gone = sorted(ROSTER - {m['id'] for m in p.g.state['party']})
    if not gone or p.foes(): return
    st = p.key('c', 'cast')
    if not st['cast']: p.idle(); return
    st = p.g.type('kl'); p.idle()
    left = sorted(ROSTER - {m['id'] for m in p.g.state['party']})
    p.log('  help for %s: %r at (%d,%d), still gone %s' % (gone, st['msg'][-60:], st['X'], st['Y'], left))
    if left: p.issue('train', 'Help did not raise %s: %r' % (left, st['msg'][-80:]))

def grind(p, ex, ey, rounds=4):
    """step in and out of the egg's range: each approach is one hatch roll; fight what comes"""
    pos = SITE_GATE.get((ex, ey)) or min((k for k in ORB if ORB_LEVEL.get(k, 0) == 0), key=lambda k: max(abs(ORB[k][0] - ex), abs(ORB[k][1] - ey)))
    xp0 = sum(m.get('exp', 0) for m in p.g.state['party'])
    for r in range(rounds):
        st = p.g.state
        if st['L'] != 0 or max(abs(st['X'] - ex), abs(st['Y'] - ey)) > 70:   # first, or after the recall to the castle
            if not orb_go(p, pos): return
        try:
            far = ring_spot(p, ex, ey, 22, 26)
            if far: p.walk(*far)
            near = ring_spot(p, ex, ey, 11, 15)
            if not near: p.issue('train', 'no way near the egg at (%d,%d)' % (ex, ey)); return
            p.walk(*near)
            if r == 0:                                                   # what hatches there must be in walking reach
                x, y = p.nav.pos()
                if p.nav.route(x, y, {(ex + a, ey + b) for a in (-1, 0, 1) for b in (-1, 0, 1)}, limit=60000) is None:
                    p.log('  grind (%d,%d): the egg is out of walking reach, skipped' % (ex, ey)); GRIND_SKIP.add((ex, ey)); return
            for _ in range(6):                                           # let them come
                if p.g.state['foes'] and p.foes(): break
                p.key('space', 'wait for the hatch')
            if not p.foes():                                             # they keep to the egg beyond the party's sight: go to them
                hatched = [a for a in p.g.actors() if a['id'] >= 224 and a.get('hostile') and a['hp'] > 0
                           and max(abs(a['x'] - ex), abs(a['y'] - ey)) <= 8]
                if hatched:
                    try: p.nav.goto(ex, ey, near=5, on_step=lambda s_: s_['foes'] > 0, max_steps=40)
                    except NavError as e: p.log('  to the hatchlings: %s' % e)
            if p.foes(): p.fight()
            raise_dead(p)
            p.cure_up(); p.heal_up(); p.rest_if_hurt(0.6)
        except (NavError, Danger) as e: p.log('  grind (%d,%d): %s' % (ex, ey, e)); p.rest_if_hurt(0.9)
        if p.g.state['over']: return
    p.log('  grind (%d,%d) x%d: +%d xp: %s' % (ex, ey, rounds, sum(m.get('exp', 0) for m in p.g.state['party']) - xp0, party_line(p)))

def seg_train(p, kw):
    """earn the levels for the Gargoyle half (the user's choice, 2026-11-07): hunt at the egg sites, meditate
    when anyone has the experience; until everyone is level TRAIN_TO (env, default 6) or the time runs out"""
    goal = int(os.environ.get('TRAIN_TO', '6')); t_end = time.time() + 60 * float(os.environ.get('TRAIN_MIN', '60'))
    p.log('  train from: %s' % party_line(p))
    ROSTER.update(m['id'] for m in p.g.state['party'])
    p.g.actors(); ROSTER.update(kv(l)['id'] for l in p.g.got if l.startswith('@L '))   # the fallen still belong (F2 dump)
    raise_dead(p)
    global GRIND
    GRIND = hunt_sites(p) or GRIND
    i = 0
    while time.time() < t_end and not all(m['lvl'] >= goal for m in p.g.state['party']):
        if not carried(p, 87): p.issue('train', 'the Orb of the Moons is no longer carried'); break
        sites = [g_ for g_ in GRIND if g_ not in GRIND_SKIP]
        if not sites: p.issue('train', 'no hunting site left'); break
        ex, ey = sites[i % len(sites)]; i += 1
        for obj in (68, 71, 67):                                       # the caster pays from his own pack (magic_skills.md)
            for _ in range(4):
                if count(p, obj) == count(p, obj, 1) or not give_to_avatar(p, obj, others=True): break
        p.cure_up(); p.heal_up()
        grind(p, ex, ey)
        raise_dead(p)
        lv = sum(m['lvl'] for m in p.g.state['party'])
        meditate(p)
        keep_up(p)
        if sum(m['lvl'] for m in p.g.state['party']) > lv: checkpoint(p, p.g.hd, p.out, 'trainauto')   # a crash keeps the levels
    p.log('  train to: %s' % party_line(p))

def seg_camp(p, kw):
    """camp 8 hours by the Cove gate, where the Avatar died in camp during training (a check)"""
    if not orb_go(p, 4): return
    for _ in range(3):
        msg = p.rest_if_hurt(1.01)
        p.log('  camp: %r' % msg)

def seg_levertest(p, kw):
    """U)se aimed at someone standing on a lever (usecode_use.md, What U)se takes): the lever under them is used,
    by the Avatar alone (Solo: the one on the lever stays) and by another member alone"""
    reach = reach_set(p); x0, y0 = p.nav.pos()
    objs = [o for o in p.g.objects()[0] if o['obj'] in (268, 174) and (o['x'], o['y']) in reach]
    if not objs: p.issue('levertest', 'no lever to stand on here'); return
    a = min(objs, key=lambda o: abs(o['x'] - x0) + abs(o['y'] - y0)); lx, ly = a['x'], a['y']
    p.log('  lever %d at (%d,%d) q%d frame tile %s' % (a['obj'], lx, ly, a['q'], a.get('tile')))
    st = p.key('2', 'member 2 alone'); p.walk(lx, ly)
    if p.nav.pos() != (lx, ly): p.issue('levertest', 'member 2 did not reach the lever: %s' % (p.nav.pos(),)); p.key('0'); return
    on = lambda: [(a['id'], a['x'], a['y']) for a in p.g.actors() if abs(a['x'] - lx) + abs(a['y'] - ly) <= 1]
    p.log('  member 2 there: %s' % on())
    st = p.key('2', 'the Avatar alone'); p.log('  the Avatar (2nd in a solo roster) %r: %s' % (st['msg'][-30:], on())); p.walk(lx, ly, adj=True); px, py = p.nav.pos()   # solo numbers follow the roster, soloist first (0 refuses with the party apart)
    p.log('  on the lever: %s' % [a['id'] for a in p.g.actors() if (a['x'], a['y']) == (lx, ly)])
    p.key('u'); st = p.key(VDIR[(lx - px, ly - py)], 'use'); p.log('  Avatar solo: %r' % st['msg'][-60:])
    if 'Use-a lever' not in st['msg']: p.issue('levertest', 'Avatar solo: %r' % st['msg'][-60:])
    st = p.key('3', 'member 3 alone'); p.walk(lx, ly, adj=True); px, py = p.nav.pos()
    p.key('u'); st = p.key(VDIR[(lx - px, ly - py)], 'use'); p.log('  member 3 solo: %r' % st['msg'][-60:])
    if 'Use-a lever' not in st['msg']: p.issue('levertest', 'member 3 solo: %r' % st['msg'][-60:])
    p.key('0', 'party mode')

def seg_cheat(p, kw):
    """Iolo's own cheat menu (usecode_vm.md): spam x3, humbug, Edit player, Set Quest Flag 1; the menu must read
    the flag back, and the way to the Codex then lets the party by (a check; never saved)"""
    out = p.talk_to(4, ['spam', 'spam', 'spam', 'humbug', '5', '1', '1'], 'Iolo') or []
    text = ' '.join(r for _, r in out)
    p.log('  cheat menu: %r' % text[-300:])
    if 'Cheaters Menu' not in text: p.issue('cheat', 'no cheat menu: %r' % text[-200:]); return
    if 'Quest flag = 1' not in text: p.issue('cheat', 'Set Quest Flag did not read back: %r' % text[-200:])
    while p.g.state['talk']: p.key('esc', 'leave the talk')
    if not orb_go(p, 19): return
    try: p.nav.goto(923, 872)
    except NavError as e: p.log('  to the Codex way: %s' % e)
    st = p.g.state
    p.log('  Codex way: at (%d,%d) msg=%r' % (st['X'], st['Y'], st['msg'][-80:]))
    if 'denied' in st['msg'].lower(): p.issue('cheat', 'on the quest, the way to the Codex still refused')

def readied(p):
    """the party's readied things: {(actor, slot): obj} from the F3 dump's @E lines"""
    p.g.objects(); return {(kv_['id'], kv_['slot']): kv_['obj'] for kv_ in (kv(l) for l in p.g.got if l.startswith('@E '))}

def seg_eqcheck(p, kw):
    """S)ave and restore in place: what the party has readied must come back (save_load.md; a check)"""
    equip_party(p); e0 = readied(p)
    hd = p.g.hd; checkpoint(p, hd, p.out, 'eqcheck'); restore(p, 'eqcheck'); e1 = readied(p)
    p.log('  readied before %s / after %s' % (sorted(e0.items()), sorted(e1.items())))
    if e0 != e1: p.issue('save', 'readied things changed across save and restore: %s -> %s' % (sorted(e0.items()), sorted(e1.items())))

SEGS = dict(usekit=seg_usekit, castle=seg_castle,panel=seg_panel, look=seg_look,  book=seg_book, parchment=seg_parchment, bridge=seg_bridge, britain=seg_britain, shop=seg_shop, sell=seg_sell, death=seg_death, healer=seg_healer, help=seg_help, cave=seg_cave, farm=seg_farm, trade=seg_trade, paid=seg_paid, orb=seg_orb, shrine=seg_shrine, justice=seg_justice, honor=seg_honor, spirituality=seg_spirituality, humility=seg_humility, sacrifice=seg_sacrifice, valor=seg_valor, honesty=seg_honesty, shrines=seg_shrines, gargish=seg_gargish, gargoyles=seg_gargoyles, lenses=seg_lenses, cube=seg_cube, keg=seg_keg, gate=seg_gate, sutek=seg_sutek, catacombs=seg_catacombs, balloon=seg_balloon, singularity=seg_singularity, statues=seg_statues, ending=seg_ending, train=seg_train, camp=seg_camp, cheat=seg_cheat, levertest=seg_levertest, eqcheck=seg_eqcheck, meditate=lambda p, kw: meditate(p))

QUEST = ('justice', 'honor', 'spirituality', 'humility', 'sacrifice', 'valor', 'honesty', 'shrines', 'gargish')   # the route's chapters (bot_walkthrough.md §3)
CKPT_FILES = ('SAVED.000', 'SAVES.DAT')                              # slot 1 and the slot index (save_load.md §2b)

def ckpt_dir(out, name):
    return name if os.sep in name else os.path.join(os.environ.get('BOT_CKPT', os.path.join(out, 'ckpt')), name)

def checkpoint(p, hd, out, name):
    """S)ave to slot 1 and keep a copy of it on the host (bot_walkthrough.md §2)"""
    import shutil
    p.idle(); p.key('s', 'the slot page'); p.key('1', 'slot 1'); st = p.key('ret', 'save')
    d = ckpt_dir(out, name); os.makedirs(d, exist_ok=True)
    for f in CKPT_FILES: shutil.copyfile(os.path.join(hd, f), os.path.join(d, f))
    p.log('  checkpoint %s: %r at L%d (%d,%d) day %d %02d:%02d' % (name, st['msg'], st['L'], st['X'], st['Y'], st['day'], st['min'] // 60, st['min'] % 60))

def restore(p, name):
    """after boot: Ctrl-R, slot 1, Enter (the checkpoint was copied into the drive folder before boot)"""
    p.g.key('r', ctrl=True); p.key('1', 'slot 1'); st = p.key('ret', 'restore')
    p.nav.refresh()
    p.log('  restored %s: %r at L%d (%d,%d) day %d %02d:%02d' % (name, st['msg'], st['L'], st['X'], st['Y'], st['day'], st['min'] // 60, st['min'] % 60))

def main():
    import shutil
    args = sys.argv[1:]; start = None
    if '--from' in args: i = args.index('--from'); start = args[i + 1]; del args[i:i + 2]
    hd, out = args[0], os.path.abspath(args[1]); segs = args[2:] or [s for s in SEGS if s not in ('book', 'parchment', 'usekit')]
    os.makedirs(out, exist_ok=True)
    if start:                                                          # the checkpoint's slot into the drive folder
        for f in CKPT_FILES: shutil.copyfile(os.path.join(ckpt_dir(out, start), f), os.path.join(hd, f))
    logf = open(os.path.join(out, 'run.log'), 'a')
    def log(s):
        line = '%s %s' % (time.strftime('%H:%M:%S'), s); print(line, flush=True); logf.write(line + '\n'); logf.flush()
    kw = keywords(REF)
    demo = os.path.join(hd, 'MAGIC.REQ')                               # the test stock, but for the segments of the real rules
    if segs in (['book'], ['bookload'], ['parchment']):
        if os.path.exists(demo): os.unlink(demo)
    else: open(demo, 'w').write('1')
    g = Game(hd, out, machine=os.environ.get('BOT_MACHINE', 'falcon')); p = Player(g, out, REF, log)
    t0 = time.time()
    try:
        if start: restore(p, start)
        for s in segs:
            if s.startswith('save:'): checkpoint(p, hd, out, s[5:]); continue
            log('== segment %s' % s); ts = time.time()
            p.heal_up(); p.cure_up()
            p.loot_ok = s not in QUEST
            if s in QUEST: keep_up(p)
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
