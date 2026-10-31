#!/usr/bin/env python3
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

class Player:
    def __init__(self, game, outdir, refdir, log=print):
        self.g, self.out, self.log, self.ref = game, outdir, log, refdir
        self.nav = Nav(game, log)
        self.issues = []; self.nshot = 0; self.transcripts = {}
        self.ilog = open(os.path.join(outdir, 'issues.jsonl'), 'a')

    # ---- issue log ------------------------------------------------------------------------------
    def issue(self, kind, detail, shot=True):
        st = self.g.state or {}
        rec = dict(kind=kind, detail=detail, L=st.get('L'), X=st.get('X'), Y=st.get('Y'), k=st.get('k'),
                   msg=st.get('msg', ''), t=time.strftime('%H:%M:%S'))
        if shot:
            self.nshot += 1; rec['shot'] = self.g.shot('issue%03d.png' % self.nshot)
        self.issues.append(rec); self.ilog.write(json.dumps(rec) + '\n'); self.ilog.flush()
        self.log('ISSUE %s: %s' % (kind, detail))

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
            try: self.nav.goto(a['x'], a['y'], near=r, max_steps=400)
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

    def talk_to(self, aid, words=None, name=''):
        """talk to actor aid and say each word; returns [(word, reply)] or None if not reached"""
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
            for _ in range(8):                                     # a y/n or number prompt is pending
                if not (self.g.state['ask'] and (self.g.state.get('askc') or self.g.state.get('num'))): break
                st = self.say('n'); out.append(('n/#', st['conv']))
            if not self.g.state['ask']: break
            st = self.g.type(w); self.check('say %r to %d' % (w, aid))
            reply = st['conv']
            if '> ' + w not in reply: self.issue('talk', '%d %s: no reply to %r' % (aid, name, w), shot=False)
            elif not reply.split('> ' + w, 1)[1].strip(): self.issue('talk', '%d %s: empty reply to %r' % (aid, name, w), shot=False)
            out.append((w, reply))
        for _ in range(4):
            if not self.g.state['ask']: break
            st = self.say('y' if self.g.state.get('askc') else 'bye'); out.append(('bye/y', st['conv']))
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
                out = self.talk_to(a['id'], [w for w in words if w not in SKIP_WORDS], name)
                if out is not None: done.add(a['id'])
        return done
