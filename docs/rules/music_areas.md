# Rules: per-area music selection (which track plays where)

Games: U6 / Savage / Martian — default = U6; deltas below
Implemented by: /render gamedemo `music_area_group()` / `music_group_pick()` / `music_update_area()`
(M10); calls the music manager `music_play_song(base)` which picks the backend (MIDI / DMA / PSG).
Tracked in: /docs/INDEX.md  ·  Milestone: M10 (audio: music)

> Provenance
> - Derived from : the U6 `.M` set + MIDI.DAT song titles (docs/formats/music.md); and by READING how
>   the original selects music, as reimplemented in Nuvie's `sound/SoundManager.cpp`
>   (`LoadNativeU6Songs` + the group / `musicPlayFrom` / `RequestSong`→`SoundCollection::Select`
>   mechanism). Read to UNDERSTAND, not transcribed; implemented from this spec.
> - References   : Nuvie `sound/SoundManager.cpp` (group→song table, random-pick collections); the
>   MIDI.DAT song titles ("Engagement and Melee", "Dungeon", "Rule Britannia", …); Ultima Codex.
> - Tools        : Hatari MIDI capture (program-changes identify the running song).
> - Validated by : mechanism (auto-start + combat switch) verified on-target; the GROUP→SONG table is
>   taken from Nuvie's native-U6 loader (the authoritative reimplementation). The exact runtime TRIGGERS
>   for the boat/dungeon/gargoyle groups are not yet wired (those game systems don't exist here yet).
> - Confidence   : group model + song-group table = confirmed (from Nuvie). Trigger wiring = partial
>   (combat done; boat/dungeon/gargoyle pending the underlying systems).
> - Last updated : 2026-07-16

## 1. System (GROUP-based, faithful to the original)
U6 does NOT map screen position to a specific song. It sorts its songs into GROUPS and, when the game
CONTEXT changes, plays a track from the group that context calls for. The wandering group holds several
themes and a track is picked at RANDOM; the other groups hold a single track. The game starts on the
`random` group (`musicPlayFrom("random")`).

The engine mirrors this: `music_area_group()` maps the current context to a group; on a group CHANGE,
`music_group_pick()` returns a track from it (random within the wandering pool, via the deterministic
RNG seam) and the manager switches to it. It runs each turn in the main loop beside the DMA pump; the
first call (on entering play) selects the `random` group, so a wandering theme auto-starts with no
external trigger. A "forced" song (the SONG.REQ test hook, or a future scripted cutscene cue) pins the
music and suppresses auto-switching.

## 2. Groups → songs (from Nuvie `LoadNativeU6Songs`)
| Group      | Songs (`.M`)                              | Context / trigger                         | Wired? |
|------------|-------------------------------------------|-------------------------------------------|--------|
| `random`   | BRIT, FOREST, STONES, ULTIMA (random pick)| overworld AND towns — the default/wandering| yes    |
| `combat`   | ENGAGE                                     | combat mode active                        | yes    |
| `dungeon`  | DUNGEON                                    | underground / in a dungeon                | no¹    |
| `boat`     | HORNPIPE                                   | aboard a ship                             | no²    |
| `gargoyle` | GARGOYLE                                   | the gargoyle lands                        | no³    |
| (front-end)| INTRO, CREATE, BOOTUP, END                | intro / chargen / boot / endgame          | n/a⁴   |

Song titles (MIDI.DAT): BRIT="Rule Britannia", FOREST="Wanderer/Forest", STONES="Stones",
ULTIMA="Ultima Theme", ENGAGE="Engagement and Melee", DUNGEON="Dungeon",
HORNPIPE="Captain Johne's Hornpipe", GARGOYLE="Audchar Gargl Zenmur".

Priority: combat overrides the location groups. Only a group CHANGE re-cues; steady state is a cheap
integer compare each turn. Leaving a group (e.g. combat ends) re-requests the location group, which for
`random` re-picks a wandering track (Nuvie behaves the same).

¹ needs dungeon navigation.  ² needs ship/boarding state.  ³ needs gargoyle-region detection.
⁴ driven by the front-end sequences, not this hook.

## 3. Per-game deltas -> RuleSet hooks
- U6 (default): the groups above.
- Savage Empire / Martian Dreams: their own `.M` sets + group mapping; slot behind a per-game music
  policy (a `GameDef` field or a `RuleSet` hook `area_group(ctx)`), NOT an engine fork.

## 4. Determinism / RNG
The wandering pick is RANDOM in the original. We seed a small LCG from the 200 Hz timer at boot (varies
per session) and pick the high bits; it is independent of the game-logic RNG so music never perturbs
gameplay determinism. Headless tests pin an exact song via SONG.REQ (the forced path), so the random
pick never destabilises a test.

## 5. Test hooks
- On-target: boot with a SONG.REQ (forced) to pin an exact track and capture MIDI; program-changes must
  match that track's MIDI.DAT map (validated for ULTIMA). WITHOUT a forced song the auto-started track
  is a random one of the four wandering themes — assert it is one of {BRIT,FOREST,STONES,ULTIMA}, not a
  single fixed song. Toggle combat and confirm the switch to ENGAGE (needs an in-game encounter — not
  yet automated headless).

## 6. Open questions
- [ ] Wire the boat/dungeon/gargoyle triggers once ship/dungeon/region state exists (the group table is
      already correct; only `music_area_group()` gains the checks).
- [ ] Pool ROTATION: the original re-picks a new random wandering track when the current one ENDS. We
      loop the current track seamlessly instead (M10 DMA loop work). Decide whether to rotate on song-end
      for closer fidelity, or keep seamless single-track loops (an enhancement).
- [ ] Confirm the exact original trigger for the `gargoyle` group (region vs event) and any `boat`
      nuance (skiff vs ship) against observation of the original.
- [ ] Scripted/usecode music cues (events, specific NPCs) — route through the "forced song" path.

## 7. Status
IN PROGRESS — group model + song-group table VERIFIED (from Nuvie's native-U6 loader); auto-start +
combat switch wired and mechanism-verified. Remaining: wire boat/dungeon/gargoyle triggers when those
systems land; decide on pool rotation vs seamless single-track loops.
