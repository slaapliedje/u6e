#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""play.py: game actions on top of u6bot.Game + nav.Nav, with an issue log.

Player.talk_to(id, words) walks next to an NPC, T)alks in its direction and types the words; the
replies are collected. Player.issue() records anything suspicious with a screenshot; check() runs
the generic checks after every action. Esc is never sent outside text entry: in u6e it quits."""
import json, os, re, subprocess, time
from nav import Nav, NavError, DIRV, VDIR

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SKIP_WORDS = {'join', 'leav', 'bye'}          # the sweep must not reshape the party; bye ends it

def keywords(refdir):
    exe = os.path.join(ROOT, 'build', 'convkw')
    if not os.path.exists(exe): subprocess.run(['make', '-C', ROOT, 'convkw'], check=True, capture_output=True)
    out = {}
    for ln in subprocess.run([exe, refdir], check=True, capture_output=True, text=True).stdout.splitlines():
        f = ln.split(' ', 2)
        if len(f) >= 2: out[int(f[0])] = (f[1].replace('_', ' '), [w for w in (f[2].split(',') if len(f) > 2 else [])
                                                                   if w and all(c.isalnum() for c in w)])
    return out

def sections(refdir, npc):
    """[(text before, [keywords])] of an NPC's keyword sections (convkw <dir> <npc>)"""
    exe = os.path.join(ROOT, 'build', 'convkw')
    r = subprocess.run([exe, refdir, str(npc)], capture_output=True, text=True, errors='replace')
    out = []
    for ln in r.stdout.splitlines():
        if '\t' in ln:
            q, k = ln.rsplit('\t', 1); out.append((q.strip(), [w for w in k.split(',') if w and w != '*']))
    return out

class Danger(Exception): pass                  # a member is near death with foes about: stop what you are doing

class Player:
    def __init__(self, game, outdir, refdir, log=print):
        self.g, self.out, self.log, self.ref = game, outdir, log, refdir
        self.nav = Nav(game, log); self.nav.keyfn = lambda k: self.key(k, 'walk')
        self.issues = []; self.nshot = 0; self.transcripts = {}
        self.ilog = open(os.path.join(outdir, 'issues.jsonl'), 'a')
        self.dead_k = -1; self.hp_seen = {}; self.meant = set(); self.looted = set(); game.watch = self.watch   # meant: companions killed on purpose

    # ---- issue log ------------------------------------------------------------------------------
    def issue(self, kind, detail, shot=True):
        st = self.g.state or {}
        rec = dict(kind=kind, detail=detail, L=st.get('L'), X=st.get('X'), Y=st.get('Y'), k=st.get('k'),
                   msg=st.get('msg', ''), t=time.strftime('%H:%M:%S'))
        if shot:
            self.nshot += 1; rec['shot'] = self.g.shot('issue%03d.png' % self.nshot)
        self.issues.append(rec); self.ilog.write(json.dumps(rec) + '\n'); self.ilog.flush()
        self.log('ISSUE %s: %s' % (kind, detail))

    def watch(self, st):
        """every report, dumps included: the Avatar's death (Lord British's resurrection, combat.md §3d)"""
        if 'unending darkness' in st.get('msg', '').lower() and self.dead_k < 0:
            self.dead_k = st['k']
            self.issue('death', 'the Avatar died; now L%d (%d,%d)' % (st['L'], st['X'], st['Y']))
        elif 'unending darkness' not in st.get('msg', '').lower(): self.dead_k = -1
        hp = {m['id']: m['hp'] for m in st.get('party', [])}
        for i, h in hp.items():                                      # a companion falls (combat.md §3d), in or out of a fight
            if i != 1 and h == 0 and self.hp_seen.get(i, 0) > 0 and i not in self.meant:
                self.issue('combat', 'party member %d died (L%d (%d,%d), %r)' % (i, st['L'], st['X'], st['Y'], st.get('msg', '')[:80]), shot=False)
        self.hp_seen = hp

    def near_death(self, frac=0.35):
        return [m['id'] for m in self.g.state['party'] if m['max'] and 0 < m['hp'] < m['max'] * frac]

    def check(self, what=''):
        st = self.g.state
        for ln in self.g.got:                                  # console output that is not ours
            if ln.startswith(('command FIFO', 'WARN :', 'INFO :')): continue   # Hatari's own
            if not ln.startswith('@') and ln.strip(): self.issue('console', '%s: %s' % (what, ln[:200]), shot=False)
        text = st.get('msg', '') + ' ' + st.get('conv', '')
        for pat in ('not yet implemented', 'not implemented', 'vm error', 'todo'):
            if pat in text.lower(): self.issue('text', '%s: %r' % (what, text[:200]))
        if st.get('over'): self.issue('game-over', what)


    def key(self, k, what=''):
        st = self.g.key(k); self.check(what or str(k)); return st

    def idle(self):
        """leave any text entry or armed verb (Esc is safe there; in plain play it would quit)"""
        for _ in range(3):
            st = self.g.state
            if st['ask'] or st['cast'] or st['mantra'] or st['verb']: self.key('esc')
            else: return

    # ---- people ---------------------------------------------------------------------------------
    def actor(self, aid):
        for a in self.g.actors():
            if a['id'] == aid: return a
        return None

    def approach(self, aid, r=3, tries=6):
        """walk until actor aid stands within r tiles (they may walk away meanwhile); returns its offset"""
        for _ in range(tries):
            a = self.actor(aid)
            if not a: return None
            x, y = self.nav.pos(); d = (a['x'] - x, a['y'] - y)
            if max(abs(d[0]), abs(d[1])) <= r: return d
            try: self.walk(a['x'], a['y'], near=r, max_steps=400)          # fights whatever attacks on the way
            except NavError as e: self.log('  approach %d: %s' % (aid, e)); return None
        return None

    def say(self, word):
        """answer the open conversation prompt: one key at a y/n prompt, digits at a number prompt"""
        st = self.g.state
        if st.get('askc'):
            c = word[:1].lower() if word[:1].lower() in st['askc'].lower() else ('n' if 'n' in st['askc'].lower() else st['askc'][0].lower())
            return self.g.char(c)
        if st.get('num') and not word.isdigit():
            conv = st.get('conv', '')
            if 'which of you' in conv.lower():                 # a shop: each member in turn, then nobody
                q = conv.rstrip()[-40:]; n = self.numtry.get(q, 0) + 1; self.numtry[q] = n
                word = str(n) if n <= len(st.get('party', [])) else '0'
            else: word = '0'
        return self.g.type(word)

    def answer(self, aid, conv):
        """if the text ends with a question the script itself answers, its first keyword"""
        tail = conv.rstrip()
        if not tail.endswith(('?', '?"')): return None
        for q, kws in sections(self.ref, aid):
            qq = q.strip().strip('"').strip()
            if len(qq) > 12 and qq[-30:] in tail and kws: return kws[0]
        return None

    def talk_to(self, aid, words=None, name='', yes=False):
        """talk to actor aid and say each word (a callable word picks one from the transcript so far, None
        ends); returns [(word, reply)] or None if not reached. A y/n prompt pending before a word is
        answered n (yes=True: y)"""
        d = self.approach(aid)
        if not d: self.log('  could not reach %d %s' % (aid, name)); return None
        a = self.actor(aid); x, y = self.nav.pos()                 # T)alk-: walk the crosshair onto them, Enter
        if a: d = (a['x'] - x, a['y'] - y)
        st = self.key('t', 'talk')
        if st['verb'] != 'T': self.issue('talk', 'T did not arm Talk- (verb=%r)' % st['verb'])
        for _ in range(abs(d[0])): self.key('right' if d[0] > 0 else 'left')
        for _ in range(abs(d[1])): self.key('down' if d[1] > 0 else 'up')
        st = self.key('ret', 'talk %d' % aid)
        if st['talk'] != aid:
            self.issue('talk', 'T)alk to %d %s (%s) gave talk=%s msg=%r' % (aid, name, d, st['talk'], st['msg']))
            return None
        out = [('', st['conv'])]; self.numtry = {}
        for _ in range(6):                                      # the script poses questions first
            ans = self.answer(aid, st['conv'])
            if not ans or not st['ask']: break
            st = self.say(ans); self.check('answer %r to %d' % (ans, aid)); out.append((ans, st['conv']))
        for w in (words or []):
            if not self.g.state['ask']: break
            if callable(w):                                        # a word chosen from what was said so far
                w = w(self.g.state['conv'])
                if w is None: break
            for _ in range(16):                                    # a y/n or number prompt is pending (a shop: each member, then 0)
                if not (self.g.state['ask'] and (self.g.state.get('askc') or self.g.state.get('num'))): break
                if self.g.state.get('num') and w.isdigit(): break             # the planned answer to this prompt
                if self.g.state.get('askc') and len(w) == 1 and w in self.g.state['askc'].lower(): break
                a = 'y' if yes and self.g.state.get('askc') else 'n'
                st = self.say(a); out.append((a + '/#', st['conv']))
            if not self.g.state['ask']: break
            st = self.say(w) if (self.g.state.get('askc') and len(w) == 1) else self.g.type(w)
            self.check('say %r to %d' % (w, aid))
            reply = st['conv']
            if '> ' + w not in reply: self.issue('talk', '%d %s: no reply to %r' % (aid, name, w), shot=False)
            elif not reply.split('> ' + w, 1)[1].strip(): self.issue('talk', '%d %s: empty reply to %r' % (aid, name, w), shot=False)
            out.append((w, reply))
        yn, prev = ('y' if yes else 'n'), None                # a y/n after bye: 'n' (no purchases) or 'y', the other key if it asks again
        for _ in range(16):
            if not self.g.state['ask']: break
            tail = self.g.state['conv'].rstrip()[-60:]
            if self.g.state.get('askc') and tail == prev: yn = 'n' if yn == 'y' else 'y'
            prev = tail if self.g.state.get('askc') else None
            st = self.say(yn if self.g.state.get('askc') else 'bye'); out.append(('bye/' + yn, st['conv']))
        if self.g.state['ask']:
            self.issue('talk', '%d %s: still asking after bye' % (aid, name)); self.key('esc')
        self.idle()
        self.transcripts.setdefault(aid, []).append(out)
        return out

    def sweep(self, box, kw, done=None, party=()):
        """talk to every actor standing in box=(x0,y0,x1,y1) on this level, all their keywords"""
        done = set() if done is None else done
        for rnd in range(3):                                   # people walk in and out of the box
            todo = [a for a in self.g.actors() if box[0] <= a['x'] <= box[2] and box[1] <= a['y'] <= box[3]
                    and a['id'] not in done and a['id'] != 1 and a['id'] in kw and not a['hostile']
                    and a['id'] not in party]
            if not todo: break
            x, y = self.nav.pos()
            todo.sort(key=lambda a: abs(a['x'] - x) + abs(a['y'] - y))
            for a in todo:
                name, words = kw[a['id']]
                self.log('talk %d %s (%d words)' % (a['id'], name, len(words)))
                out = self.talk_to(a['id'], [w for w in words if w not in SKIP_WORDS and len(w) > 1], name)   # y/n are answers, not topics
                if out is not None: done.add(a['id'])
        return done

    # ---- combat ------------------------------------------------------------------------------
    def foes(self):
        """hostile actors alive within 8 tiles that the party can walk up to, nearest first (a foe
        behind rock, or across water, is not a fight)"""
        x, y = self.nav.pos(); self.nav.ensure()
        fs = [a for a in self.g.actors() if a['id'] != 1 and a.get('hostile') and a['hp'] > 0
              and max(abs(a['x'] - x), abs(a['y'] - y)) <= 8]
        fs = [a for a in fs if max(abs(a['x'] - x), abs(a['y'] - y)) <= 1 or self.approach_path(a) is not None]
        return sorted(fs, key=lambda a: max(abs(a['x'] - x), abs(a['y'] - y)))

    def approach_path(self, f):
        x, y = self.nav.pos()
        goal = {(f['x'] + ddx, f['y'] + ddy) for ddx in (-1, 0, 1) for ddy in (-1, 0, 1) if ddx or ddy}
        goal = {t for t in goal if self.nav.cost(t[0], t[1]) is not None or t == (x, y)}
        if (x, y) in goal: return []
        return self.nav.route(x, y, goal, limit=20000) if goal else None

    def fight(self, max_turns=150, loot=True):
        """combat mode on; A)ttack the nearest foe when adjacent, else step toward it; until no foes are
        near, then loot the fallen. Issues: a fight where no foe loses hp for 40 turns, a party member
        dying, game over."""
        before = {o['i'] for o in self.g.objects()[0]} if loot else None
        if not self.g.state['combat']: self.key('b', 'combat on')
        stale = turns = misses = 0; seen = {}; msgs = []; hp0 = {p['id']: p['hp'] for p in self.g.state['party']}
        self.log('  fight at (%d,%d) L%d: %s' % (self.g.state['X'], self.g.state['Y'], self.g.state['L'],
                 ', '.join('#%d tile %d hp %d/%d' % (a['id'], a['tile'], a['hp'], a['max']) for a in self.foes())))
        while turns < max_turns:
            st = self.g.state
            if st['over']: self.issue('combat', 'game over in a fight'); return False
            fs = self.foes()
            if not fs: break
            total = sum(a['hp'] for a in fs)                    # hp of the foes in reach
            prev = sum(seen.get(a['id'], a['hp']) for a in fs)
            stale = stale + 1 if total >= prev else 0
            for a in fs: seen[a['id']] = a['hp']
            if stale >= 40:
                who = ', '.join('%d@%d,%d hp%d' % (a['id'], a['x'], a['y'], a['hp']) for a in fs[:4])
                if misses >= 20: self.log('  the Avatar missed %d times running (%s): too nimble for him (combat.md §2), moving on' % (misses, who))
                else: self.issue('combat', 'no foe hurt in 40 turns (%s)' % who)
                break
            f = fs[0]; x, y = self.nav.pos(); dx, dy = f['x'] - x, f['y'] - y
            if max(abs(dx), abs(dy)) <= 1:
                self.key('a', 'attack')
                if self.g.state['verb'] != 'A': self.issue('combat', 'A did not arm Attack- (verb=%r)' % self.g.state['verb']); break
                if dx: self.key('right' if dx > 0 else 'left')
                if dy: self.key('down' if dy > 0 else 'up')
                st = self.key('ret', 'attack %d' % f['id'])
                misses = misses + 1 if 'Missed' in st['msg'] else 0
                if 'not' in st['msg'].lower() and 'implemented' in st['msg'].lower(): self.issue('combat', 'attack: %r' % st['msg'])
            else:
                path = self.approach_path(f)
                if path: self.key(VDIR[(path[0][0] - x, path[0][1] - y)], 'close in')
                else: self.key('space', 'pass')
            turns += 1
            m = self.g.state['msg'].strip()
            if m and (not msgs or msgs[-1] != m): msgs.append(m)
        if msgs: self.log('  combat log: ' + ' | '.join(msgs)[:1500])
        if self.g.state['combat']: self.key('b', 'combat off')
        self.log('  fight over after %d turns: party %s' % (turns, ' '.join('%d:%d/%d' % (p['id'], p['hp'], p['max']) for p in self.g.state['party'])))
        if loot:
            if self.foes(): self.log('  no loot: foes still near')
            else: self.loot(before)
        return True

    HOLDERS = (98, 168, 211, 339, 340, 341, 347, 350, 354, 369, 372)   # searchable remains and chests (usecode_use.md)

    def clear_chests(self):
        """G)et the chests this party looted that lie beside it (a chest blocks the way, and a slain
        creature's chest can land in a narrow pass); True if one was taken"""
        objs, _ = self.g.objects(); x, y = self.nav.pos(); took = False
        for o in objs:
            if o['obj'] != 98 or o['i'] not in self.looted or abs(o['x'] - x) + abs(o['y'] - y) != 1: continue
            self.key('g'); st = self.key(VDIR[(o['x'] - x, o['y'] - y)], 'get the chest')
            self.log('  a looted chest in the way at (%d,%d): %r' % (o['x'], o['y'], st['msg']))
            took = took or 'You get' in st['msg']
        if took: self.nav.refresh()
        return took

    KEEP = (88, 77, 275, 90, 128, 129, 133)                             # gold, gems, potions, torches, food
    SELLS = ('sword', 'chain mail', 'iron helm', 'leather armor', 'leather armour', 'plate mail')   # what Max buys

    def wanted(self, o):
        n = o['name'].lower()
        for a in ('the ', 'a ', 'an '):
            if n.startswith(a): n = n[len(a):]
        return o['obj'] in self.KEEP or n in self.SELLS

    def loot(self, before, r=10, tries=24):
        """after a fight: U)se the new bodies and chests (searching spills what they hold, eggs.md §3c)
        and G)et what lies on top of the new things, until nothing new is left to take; returns the
        gold gained"""
        g0 = self.g.state.get('gold', 0); used = set(); skip = set(); took = []
        for _ in range(tries):
            objs, _ = self.g.objects(); x, y = self.nav.pos()
            new = [o for o in objs if o['i'] not in before and o['i'] not in skip
                   and abs(o['x'] - x) <= r and abs(o['y'] - y) <= r]
            tops = {}                                         # each tile's topmost new thing (the highest index)
            for o in new:
                if o['obj'] in (338, 337, 134) or (o['obj'] >= 342 and o['obj'] not in self.HOLDERS): continue   # blood, a trap, a carcass, a beast's own remains
                k = (o['x'], o['y'])
                if k not in tops or o['i'] > tops[k]['i']: tops[k] = o
            todo = [o for o in tops.values() if o['obj'] not in self.HOLDERS or o['i'] not in used]
            if not todo:
                if not took and not used: self.log('  loot: nothing new (%d new; near: %s)' % (len(new), ' '.join('%d:%d@%d,%d' % (o['i'], o['obj'], o['x'], o['y']) for o in objs if abs(o['x'] - x) <= 3 and abs(o['y'] - y) <= 3)))
                break
            o = min(todo, key=lambda o: abs(o['x'] - x) + abs(o['y'] - y))
            try:
                if abs(o['x'] - x) + abs(o['y'] - y) != 1: self.nav.refresh(); self.nav.goto(o['x'], o['y'], adj=True); x, y = self.nav.pos()
            except Exception as e: self.log('  loot at (%d,%d): %s' % (o['x'], o['y'], e)); skip.add(o['i']); continue
            if abs(o['x'] - x) + abs(o['y'] - y) != 1: skip.add(o['i']); continue
            d = VDIR[(o['x'] - x, o['y'] - y)]
            if o['obj'] in self.HOLDERS:
                used.add(o['i']); self.looted.add(o['i']); self.key('u'); st = self.key(d, 'search %d' % o['obj'])
                if 'nothing' in st['msg'].lower(): skip.add(o['i'])
            elif not self.wanted(o):                          # not worth its weight: leave it, and what lies under it
                skip.add(o['i'])
            else:
                self.key('g'); st = self.key(d, 'get %d' % o['obj'])
                if 'You get' in st['msg']: took.append(st['msg'].split('You get', 1)[1].strip(' .'))
                else: skip.add(o['i']); self.log('  get %s: %r' % (o['name'], st['msg']))
        gained = self.g.state.get('gold', 0) - g0
        if took: self.log('  looted: %s (gold %+d)' % (', '.join(took)[:400], gained))
        return gained

    def heal_up(self, frac=0.6, tries=8):
        """C)ast Heal while a member is below frac of max HP (it lands on the most wounded,
        magic_skills.md §5h); stops when a cast does not succeed (mana, reagents)"""
        n = 0
        for _ in range(tries):
            if not [m for m in self.g.state['party'] if m['max'] and 0 < m['hp'] < m['max'] * frac]: break
            st = self.key('c', 'cast')
            if not st['cast']: self.idle(); break
            st = self.g.type('heal'); self.check('cast heal')
            if 'success' not in st['msg'].lower(): self.log('  heal: %r' % st['msg']); self.idle(); break
            n += 1
        if n: self.log('  healed x%d: party %s' % (n, ' '.join('%d:%d/%d' % (m['id'], m['hp'], m['max']) for m in self.g.state['party'])))
        return n

    def poisoned(self):
        return [m['id'] for m in self.g.state['party'] if m.get('psn') and m['hp'] > 0]

    def cure_up(self):
        """each poisoned member drinks a red potion (a cure, usecode_use.md) while the pack holds one,
        else the Avatar casts Dispel Magic on them (garlic + ginseng, magic_skills.md §5b); returns the
        members still poisoned"""
        for i in self.poisoned():
            idx = [m['id'] for m in self.g.state['party']].index(i)
            st = self.use_pack(275, 'red')
            if st is not None:
                if 'On whom' not in st['msg']: self.log('  red potion: %r' % st['msg']); self.idle(); continue
                st = self.key(str(idx + 1), 'potion on %d' % i)
                self.log('  red potion on %d: %r' % (i, st['msg'][-40:]))
                if i in self.poisoned(): self.issue('cure', 'a red potion left %d poisoned (%r)' % (i, st['msg'][-60:]))
            elif not self.dispel(i): break
        return self.poisoned()

    def dispel(self, i):
        """C)ast Dispel Magic on member i: Enter for the Avatar's own tile, else the direction that
        reaches them (orthogonally in line, within 5: the first actor along it, magic_skills.md §5b); True if they
        are cured"""
        a = self.actor(i) if i != 1 else None; x, y = self.nav.pos()
        if i != 1 and not a: return False
        d = (a['x'] - x, a['y'] - y) if a else (0, 0)
        if max(abs(d[0]), abs(d[1])) > 5 or (d[0] and d[1]): return False   # the keys aim only orthogonally
        u = ((d[0] > 0) - (d[0] < 0), (d[1] > 0) - (d[1] < 0))
        st = self.key('c', 'cast')
        if not st['cast']: self.idle(); return False
        st = self.g.type('dispel magic'); self.check('cast dispel magic')
        if 'where' not in st['msg'].lower(): self.log('  dispel magic: %r' % st['msg']); self.idle(); return False
        st = self.key(VDIR[u] if u != (0, 0) else 'ret', 'dispel on %d' % i)
        if st['verb'] == 'C': self.issue('cure', 'Cast Dispel Magic still armed after aiming (%r)' % st['msg']); self.idle()
        ok = i not in self.poisoned()
        self.log('  dispel magic on %d: %r%s' % (i, st['msg'][-50:], '' if ok else ' (still poisoned)'))
        if 'success' in st['msg'].lower() and not ok: self.issue('cure', 'Dispel Magic succeeded but %d is still poisoned' % i)
        return ok

    def open_spot(self, r=8):
        """the nearest reachable tile within r whose 3x3 block is all passable, or None"""
        self.nav.refresh(); x, y = self.nav.pos(); S = self.nav.span
        cand = sorted(((x + dx) % S, (y + dy) % S) for dx in range(-r, r + 1) for dy in range(-r, r + 1))
        cand.sort(key=lambda t: abs(t[0] - x) + abs(t[1] - y))
        for cx, cy in cand:
            if all(self.nav.cost((cx + a) % S, (cy + b) % S) is not None for a in (-1, 0, 1) for b in (-1, 0, 1)) \
               and self.nav.route(x, y, {(cx, cy)}, limit=5000) is not None:
                return cx, cy
        return None

    def rest_if_hurt(self, frac=0.5, hours=8):
        """R)est when a member is below frac of max HP (wilderness / dungeon, no foes near): the
        hours and no guard; returns the message after the camp, or None if nobody needed it"""
        st = self.g.state
        hurt = [m for m in st['party'] if m['max'] and m['hp'] < m['max'] * frac]
        if not hurt or st['combat'] or st['foes']: return None
        hp0 = {m['id']: m['hp'] for m in st['party']}
        st = self.key('r', 'rest')
        if 'room' in st['msg'].lower():                                  # the camp needs the 3x3 around the Avatar open
            self.idle(); spot = self.open_spot()
            if spot:
                try: self.nav.goto(*spot, max_steps=200)
                except NavError as e: self.log('  to an open spot %s: %s' % (spot, e))
                st = self.key('r', 'rest')
        if st.get('rest') != 1:
            self.log('  rest refused: %r' % st['msg']); self.idle(); return st['msg']
        st = self.g.type(str(hours))
        if st.get('rest') == 2: st = self.g.type('0')                    # nobody guards
        for _ in range(400):                                             # the camp runs on its own; wait it out
            if not self.g.state['busy']: break
            self.key('space', 'resting')
        hp1 = {m['id']: m['hp'] for m in self.g.state['party']}
        self.log('  rested %dh: %s -> %s (%s)' % (hours, hp0, hp1, self.g.state['msg'][:120]))
        risen = [i for i in hp1 if hp0.get(i) == 0 and hp1[i] > 0]
        if risen: self.issue('rest', 'dead members %s rose by resting' % risen)
        return self.g.state['msg']

    def walk(self, tx, ty, **kw):
        """nav.goto, stopping to fight whenever foes come near; returns the final state"""
        ignore = 0; lv = self.g.state['L']                     # foes in view that cannot be reached
        for _ in range(30):
            hit = lambda s: s['foes'] or ' hits ' in s['msg'] or ' kills ' in s['msg']   # a foe out of view can still strike the rear
            try: st = self.nav.goto(tx, ty, on_step=lambda s: s['foes'] > ignore or (hit(s) and self.near_death()), **kw)
            except NavError:
                if not self.clear_chests(): raise
                continue
            if hit(self.g.state) and self.near_death():                  # under fire: heal and go on, or give up the walk
                if not self.heal_up(): raise Danger('members %s near death, foes %d in view, no heal' % (self.near_death(), self.g.state['foes']))
                continue
            if self.g.state['foes'] > ignore:
                if self.foes(): self.fight(); ignore = 0; self.heal_up(); self.cure_up(); self.rest_if_hurt()
                else: ignore = self.g.state['foes']
                if self.g.state['L'] != lv: return self.g.state     # the walk took us to another level
                continue
            if self.g.state['foes'] < ignore: ignore = self.g.state['foes']
            return st
        return self.g.state

    def use_pack(self, obj_n, name=''):
        """U)se a carried item (whose name holds `name`): U, then U until the pack prompt names it,
        Enter; the state after"""
        _, inv = self.g.objects()
        names = [r.get('name', '') for r in inv if r['obj'] == obj_n and name in r.get('name', '').lower()]
        if not names: return None
        self.key('u')
        for _ in range(len(inv) + 4):
            st = self.key('u')
            if st['inv'] and st['msg'].startswith('Use the ' + names[0] + '?'): return self.key('ret', 'use pack obj %d' % obj_n)
        self.issue('use', 'the pack prompt never named %r (last %r)' % (names[0], self.g.state['msg'])); self.idle(); return None
