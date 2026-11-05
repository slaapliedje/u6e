#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""nav.py: routes over the game's own passability dump (BOTPASS.BIN + BOTDOOR.TXT) and walks them
key by key: closed doors are opened with U)se, a tile someone stands on is waited out, then routed
around. Every step is a real arrow key; nothing is teleported."""
import heapq

DIRV = {'up': (0, -1), 'down': (0, 1), 'left': (-1, 0), 'right': (1, 0)}
VDIR = {v: k for k, v in DIRV.items()}

class NavError(Exception): pass

class Nav:
    def __init__(self, game, log=print):
        self.g, self.log = game, log
        self.level = None; self.temp = {}
        self.keyfn = game.key                                  # Player routes it through its checks

    def refresh(self):
        self.span, self.pm, doors = self.g.passmap()
        self.doors = {(x, y): fr for x, y, fr in doors}
        self.level = self.g.state['L']; self.temp = {}
        self.doors.update({k: v for k, v in getattr(self, 'locked', {}).items()})

    def ensure(self):
        if self.level != self.g.state['L']: self.refresh()

    def cost(self, x, y, keys=False):
        if (x, y) in self.temp: return None
        fr = self.doors.get((x, y))
        if fr is not None:
            if fr < 4: return 1
            if fr < 8: return 3
            if fr < 12 and keys: return 4
            return None
        return 1 if self.pm[y * self.span + x] & 1 else None

    def route(self, sx, sy, goal, keys=False, limit=400000):
        """A* from (sx,sy) to any tile in the set goal; returns the tile list (excluding the start)"""
        S = self.span
        gl = list(goal)
        def h(x, y): return min(abs(x - gx) + abs(y - gy) for gx, gy in gl) if len(gl) < 9 else \
                            abs(x - gl[0][0]) + abs(y - gl[0][1])
        openq = [(h(sx, sy), 0, sx, sy)]; came = {(sx, sy): None}; best = {(sx, sy): 0}; n = 0
        while openq:
            f, gc, x, y = heapq.heappop(openq)
            if (x, y) in goal:
                path = []; c = (x, y)
                while c != (sx, sy): path.append(c); c = came[c]
                return path[::-1]
            n += 1
            if n > limit: break
            if gc > best.get((x, y), 1 << 30): continue
            for dx, dy in DIRV.values():
                nx, ny = (x + dx) % S, (y + dy) % S
                c = self.cost(nx, ny, keys)
                if c is None: continue
                ng = gc + (c or 1)
                if ng < best.get((nx, ny), 1 << 30):
                    best[(nx, ny)] = ng; came[(nx, ny)] = (x, y)
                    heapq.heappush(openq, (ng + h(nx, ny), ng, nx, ny))
        return None

    def pos(self): return self.g.state['X'], self.g.state['Y']

    def open_door(self, x, y, d):
        st = self.keyfn('u'); st = self.keyfn(d); st = self.keyfn('ret')
        self.log('  door (%d,%d): %s' % (x, y, st['msg']))
        fr = self.doors.get((x, y))
        m = st['msg'].lower()
        if fr is not None and 4 <= fr < 8 and 'nothing' not in m and 'lock' not in m and 'not' not in m:
            self.doors[(x, y)] = fr - 4
        elif fr is not None and fr >= 4:
            self.doors[(x, y)] = max(fr, 8)                    # treat as locked: route elsewhere
        return st

    def goto(self, tx, ty, adj=False, keys=False, on_step=None, max_steps=3000, near=0):
        """walk to (tx,ty); adj=True stops orthogonally next to it (to use / get); near=r stops anywhere
        within r tiles of it (to talk through the crosshair)"""
        st = self.g.state
        if st['ask'] or st['cast'] or st['mantra'] or st['verb']:
            raise NavError('cannot walk: in a prompt (ask=%s cast=%s mantra=%s verb=%r)' % (st['ask'], st['cast'], st['mantra'], st['verb']))
        self.ensure()
        if near:
            goal = {((tx + dx) % self.span, (ty + dy) % self.span) for dx in range(-near, near + 1)
                    for dy in range(-near, near + 1) if dx or dy}
        else:
            goal = {((tx + dx) % self.span, (ty + dy) % self.span) for dx, dy in DIRV.values()} if adj else {(tx, ty)}
        goal = {t for t in goal if self.cost(t[0], t[1], keys) is not None}
        if not goal: raise NavError('no walkable tile at/next to (%d,%d)' % (tx, ty))
        steps = fails = replans = 0; refreshed = False
        if near and self.pos() in goal: return self.g.state
        while self.pos() not in goal:
            if steps >= max_steps: raise NavError('too many steps to (%d,%d)' % (tx, ty))
            x, y = self.pos()
            replans += 1
            if replans > 60: raise NavError('cannot get to (%d,%d): replanned 60 times' % (tx, ty))
            path = self.route(x, y, goal, keys)
            if not path:
                if self.temp: self.temp = {}; goal = {t for t in goal if self.cost(t[0], t[1], keys) is not None} or goal; continue
                raise NavError('no route from (%d,%d) to (%d,%d) L%d' % (x, y, tx, ty, self.g.state['L']))
            for nx, ny in path:
                d = VDIR[((nx - x + self.span // 2) % self.span - self.span // 2,
                          (ny - y + self.span // 2) % self.span - self.span // 2)]
                fr = self.doors.get((nx, ny))
                if fr is not None and fr >= 4: self.open_door(nx, ny, d)
                st = self.keyfn(d); steps += 1
                if self.g.state['L'] != self.level: return st          # a level change ends the walk
                jx = (st['X'] - x + self.span // 2) % self.span - self.span // 2
                jy = (st['Y'] - y + self.span // 2) % self.span - self.span // 2
                if abs(jx) > 2 or abs(jy) > 2: return st                 # so does a jump (a moongate, a trap door)
                if (st['X'], st['Y']) == (nx, ny):
                    x, y = nx, ny; fails = 0
                    if on_step and on_step(st): return st              # the caller takes over (foes near)
                    continue
                fails += 1                                              # someone stood there, or it is shut
                self.log('  step %s (%d,%d)->(%d,%d) failed: at (%d,%d) pm=%d door=%s msg=%r' % (d, x, y, nx, ny, st['X'], st['Y'],
                         self.pm[ny * self.span + nx], self.doors.get((nx, ny)), st['msg'][:60]))
                if not refreshed:                                       # the map may have changed under us
                    refreshed = True; keep = self.temp; self.refresh(); self.temp = keep; break
                if fails <= 2: self.keyfn('space'); break
                self.temp[(nx, ny)] = 1; fails = 0; break
        return self.g.state
