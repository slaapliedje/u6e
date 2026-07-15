# Rules: per-area music selection (which track plays where)

Games: U6 / Savage / Martian — default = U6; deltas below
Implemented by: /render gamedemo `music_area_song()` / `music_update_area()` (M10); calls the
music manager `music_play_song(base)` which picks the backend (MIDI / DMA / PSG).
Tracked in: /docs/INDEX.md  ·  Milestone: M10 (audio: music)

> Provenance
> - Derived from : the U6 `.M` set + MIDI.DAT song names (docs/formats/music.md); how the game
>   context maps to a track is by OBSERVATION + reason, not a decoded data table.
> - References   : ScummVM/Nuvie `nuvie/sound` (music is driven by game context, READ not copied);
>   the MIDI.DAT song titles ("Engagement and Melee", "Dungeon", "Ultima Theme", …).
> - Tools        : Hatari MIDI capture (program-changes identify the running song).
> - Validated by : on-target — with no forced song, the engine auto-starts the overworld theme and
>   the captured MIDI program-changes match ULTIMA's MIDI.DAT map (25/50/24/27). The combat/dungeon
>   switches are code-verified but not yet exercised in a live encounter.
> - Confidence   : mechanism = confirmed; the exact area->song assignment = **provisional**.
> - Last updated : 2026-07-14

## 1. System
The engine has one "current song." Each turn `music_update_area()` computes the song the current
game CONTEXT calls for and, only when it differs from what's playing, switches (which tears down the
old backend and starts the new track — see the music manager). It runs in the main loop beside the
DMA pump; the first call (on entering play) starts the overworld theme, so no external trigger is
needed. A "forced" song (the SONG.REQ test hook, or a future scripted cutscene cue) pins the music
and suppresses auto-switching.

## 2. Observed behavior (inputs -> outputs)
| Context                    | Track          | Confidence | Notes                                   |
|----------------------------|----------------|-----------|------------------------------------------|
| Combat mode active         | ENGAGE         | high      | "Engagement and Melee" — literal battle theme |
| In a dungeon (underground) | DUNGEON        | high      | dormant until dungeon navigation lands   |
| Overworld surface          | ULTIMA         | provisional | main "Ultima Theme"; the wandering theme |
| Towns / regions            | (overworld)    | provisional | no distinct per-town music yet           |
| Intro / chargen / endgame  | INTRO/CREATE/END | n/a     | driven by those front-end sequences, not this hook |

Priority is combat > dungeon > overworld. Only a state CHANGE reloads; steady state is a cheap
string compare each turn.

## 3. Per-game deltas -> RuleSet hooks
- U6 (default): the table above.
- Savage Empire / Martian Dreams: their own `.M` sets + context mapping; slot behind a per-game
  music policy (a `GameDef` field or a `RuleSet` hook `area_song(ctx)`), NOT an engine fork.

## 4. Determinism / RNG
None — selection is a pure function of game context. (If U6 ever randomised a wandering track, add a
seeded pick behind the deterministic RNG seam.)

## 5. Test hooks
- On-target: boot with no SONG.REQ, capture MIDI; program-changes must match the overworld track's
  MIDI.DAT map (done for ULTIMA). Toggle combat and confirm the switch to ENGAGE (needs an in-game
  encounter / the B)attle toggle — not yet automated headless).

## 6. Open questions
- [ ] Verify the OVERWORLD/town assignment against the original (does U6 use ULTIMA, STONES, or a
      wandering set on the surface? do towns get their own music?). The mechanism is agnostic — only
      the table in `music_area_song()` changes.
- [ ] Does U6 pick music per super-chunk / region (a data table), or purely by combat/dungeon state?
- [ ] Scripted/usecode music cues (events, specific NPCs) — route through the "forced song" path.

## 7. Status
IN PROGRESS — mechanism VERIFIED (auto-start + combat/overworld switch); exact area->song table
provisional pending verification against the original.
