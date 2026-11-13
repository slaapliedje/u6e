# Savage Empire: Triolo's invocations (the totems and the offerings)

Games: Savage Empire. Implemented by: /render gamedemo.c (`se_totem`, `se_offer_on`, `se_invoke_do`,
`se_status_tick` as gl_se `status_tick`). The totems' place in the use switch is se_use.md §5n.

> Provenance
> - Derived from : SE GAME.EXE (DOS), read in the RE workspace outside this repo: the use switch's totem case
>   (0x2646e), the invocation 0x12ef7, its effects 0x12d86, 0x127ab (danger), 0x129f8 (the lists), 0x1249c
>   (healing), 0x12348 (charm), 0x122af (curse), 0x1259e (the beast), the area list 0x126e4, the resist test
>   0x18662 and power 0x1862a, the uncharm 0x181bb, the time-pass wear-offs in 0x359:0x11b6, and the stat and
>   armour readers 0x185a0, 0x185e0, 0x18f74.
> - References   : none.
> - Tools        : objdump (i8086); our objname dumper.
> - Validated by  : SEUSE self-test wants "SE invoke ..." (§7); NOT yet compared with DOS running.
> - Confidence   : probable (read from the code)
> - Last updated : 2026-11-12

## 1. Who may invoke

The totems of Heluzz (86), Motazz (87) and Aphazz (88) are Triolo's (actor 9). The member acting (the one shown)
invokes when it has the totem and is Triolo, or when it hasn't the totem and the leader is Triolo. Otherwise:
`<member> can't use that!`.

`Invoke: Heluzz!`, `Invoke: Aphazz!` or `Invoke: Motazz!` on its own line. Motazz only while the party is in combat;
outside it: `Invoke: Motazz aids only those who are in mortal combat.` (with a sound) and nothing more.

Then `offering-- `: a thing from a pack, or one on the map within a tile of the leader (further: `Out of range!`; no
thing: `nothing!`). Its name is written. Chocolatl (54), pinde (55) and yopo (56) are offerings; anything else:
`Offering refused.` The offering is used up (one of a stack) when the invocation takes effect.

## 2. The nine effects

| | pinde (55) | chocolatl (54) | yopo (56) |
|---|---|---|---|
| **Heluzz** | `Triolo leaves his body and looks down at the party!`: a view of the land from above | `Triolo creates light.`: light for 200 | `Triolo senses ...` (§3) |
| **Aphazz** | party members near who are hurt or poisoned heal 1-10 (to their maximum) and are cured: `... healed.` | foes near that don't resist are charmed (§4): `... charmed.` | party members near are protected: `Aphazz protects the party.` |
| **Motazz** | foes near that don't resist are cursed: `... cursed!` | on the surface a beast comes to help (§5); below: `No help available.` | party members near go wild (the leader too, unnamed): `... wild!` |

"Near" is the 11 x 9 tiles around the leader. A foe is an actor whose alignment has the evil bit (evil or
chaotic); one already charmed or cursed is passed over, and so is anyone already healed, protected or wild.

The list is written `A, B and C are <state>` or `X is <state>`, on its own line; with nobody: `No effect.`

## 3. Danger

Every hostile (the evil bit, or worktype 0x9b) living actor on the level within 25 tiles of the party is put in
one of eight directions from the party (a distance of 0 on an axis counts as -1): north or south when its
east-west distance is under half its north-south one, east or west when the other way round, else the diagonal.
`Triolo senses no danger.`, `Triolo senses danger all around!` (all eight), or `Triolo senses danger to the
north, east and southwest.` (the directions with someone in them, in the order north, northeast, ... northwest).

## 4. Charm, curse, protection, frenzy

- **Charmed** (object status 0x04): its own alignment is kept aside, it takes the leader's (good) and worktype 8, and
  fights for the party. 1 in 25 as time passes it comes free: its alignment back (good for a party member) and, in
  combat, its combat mode.
- **Cursed** (object status 0x40): strength and dexterity 3 less (at least 1), armour 3 less, intelligence 3 less
  against invocations. 1 in 25 as time passes it lifts.
- **Protected** (actor status 0x01): armour 3 more. 1 in 25 as time passes it lifts.
- **Wild** (object status 0x80): strength and dexterity 3 more (at most 30), intelligence 3 less against
  invocations (not the leader's); a companion fights WILD (se_ai.md §4). It ends as soon as time passes out of combat.

**Resisting** (0x18662): a myrmidex drone (323) shrugs it off 1 time in 10, the queen (328) never; anyone else
when its power less the invoker's is above a roll of -12..12. Power is intelligence, 3 less if cursed or wild (but
the leader), at least 1.

## 5. The beast

On the surface, when no beast already helps: a plesiosaur (300) for a party on the water, else a sabretooth tiger
(331), at a free tile within 5 of the party (8 tries). It takes the party's alignment and worktype 8: `Success!`.
No tile, or one already helping: `Failed.` When time passes out of combat it turns chaotic, takes worktype 0x13
(flee) and helps no more.

## 6. Slips
- Slip 20: in the strength reader the wild bonus is inside the curse test, so only a cursed wild one gets it
  (the dexterity reader has it outside). The engine gives it to every wild one.
- Slip 21: the resist test takes the invoker's power from the actor numbered by the shown member's party slot
  (0 for the leader). The engine uses Triolo's.

## 7. Test hook
SEUSE self-test: `SE invoke only Triolo`, `SE invoke light`, `SE invoke heals`, `SE invoke protects`,
`SE invoke refused`, `SE invoke Motazz needs combat`, `SE invoke wild`, `SE invoke senses`, `SE invoke curses`.

## 8. Not built / open
- The sounds (item 9) and the flashes on those affected.
- The view from above is the engine's overview (U6's gem view); DOS draws its own.
- DOS's charm, curse and frenzy lists skip the several-tile creatures (0x130f:0x136); the engine does not.
- Sleep wears off 1 in 21 and only for one on screen in DOS's time pass; the engine keeps U6's roll.

## Status
IN PROGRESS: §1-§5 built (slips 20-21 as the intent).
