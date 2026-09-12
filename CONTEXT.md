# CONTEXT.md

# Garbage Collector — Project Context

This file is the persistent product and game-design context for the project.

Coding agents should read this file together with `AGENTS.md` before implementing tasks.

This document describes the current intended game. It may evolve during the jam.

Concrete implementation tasks are provided separately by the user.

---

## 1. Project

**Name:** Garbage Collector

**Platform:** Arduboy / Arduino Leonardo compatible hardware

**Theme:** Programmer's Day

**Genre:**

- Roguelike
- Arena Shooter
- Bullet Hell
- Vampire Survivors-like

**One-liner:**

A garbage collector clears runtime memory from garbage so enough memory is available to launch the main program.

---

## 2. Core Fantasy

The player is a Garbage Collector operating inside a running program.

Enemies represent garbage, runtime problems and memory-related threats.

The player survives arena stages, clears enemies, earns score/currency and upgrades the collector until the final runtime problem is defeated.

The programming theme should be visible in:

- terminology
- enemy concepts
- UI text
- upgrade names
- stage names
- Win/Game Over presentation

The game should not feel like a generic arena shooter with programming terms added only at the end.

---

## 3. Core Gameplay Loop

The intended loop is:

```text
START
  ->
STAGE
  ->
REWARD / SCORE
  ->
SHOP / UPGRADE
  ->
NEXT STAGE
  ->
...
  ->
FINAL STAGE / BOSS
  ->
WIN
```

Death flow:

```text
PLAYING
  ->
GAME OVER
  ->
RESTART / TITLE
```

During a stage:

1. The player moves around the arena.
2. The player's primary attack happens automatically.
3. Enemies attack or pressure the player.
4. The player uses active abilities with A and B.
5. Enemies give score/currency.
6. The player survives until the stage objective is complete.

Between stages:

1. The player enters an upgrade/shop screen.
2. Earned score/currency can be spent.
3. The player can improve passive stats and/or obtain active abilities.
4. The next stage begins.

---

## 4. Required Game Features

The final game must include:

- start/title screen
- score system
- stage progression
- player movement
- automatic primary attack
- active abilities
- upgrades/shop
- Game Over state
- Win state
- final stage / boss
- sound effects
- meaningful use of D-pad
- meaningful use of A
- meaningful use of B

These are high-priority requirements.

Do not remove them to make room for optional polish unless there is no other viable solution.

---

## 5. Controls

### D-pad

Moves the player around the arena.

### A

Activates the ability currently equipped in active slot A.

### B

Activates the ability currently equipped in active slot B.

A and B are symmetrical ability slots from a game-design point of view.

They are not permanently assigned to specific actions.

---

## 6. Active Ability System

The player has two active ability slots:

```text
[A] Active Slot A
[B] Active Slot B
```

Each slot contains one active ability.

The starting run includes **Dash** as one of the player's equipped active abilities.

Dash is not a permanent button action.

It is a normal active ability and may later be replaced.

This means valid future builds may include:

```text
Dash + Active Ability
Active Ability + Dash
Active Ability + Active Ability
```

A player may therefore choose to give up Dash in exchange for two other active abilities.

That tradeoff is intentional.

---

## 7. Dash

Dash is the initial mobility active ability.

Current intended characteristics:

- quickly moves the player
- has a cooldown
- may provide a short invulnerability window
- uses the active ability system
- can be replaced by another active ability

Exact values such as duration, distance and cooldown are balance parameters and are not permanently defined here.

Do not design gameplay that assumes Dash will always be equipped.

---

## 8. Primary Attack

The player's main weapon attacks automatically.

There is no dedicated manual attack button.

This keeps controls focused on:

- movement
- ability A
- ability B

The exact weapon behavior may evolve.

Cheap targeting/attack implementations are preferred because of Arduboy hardware limits.

---

## 9. Active Ability Direction

Potential active ability concepts include programming / GC-themed effects.

Examples:

### Dash

Mobility and avoidance.

### Mark & Sweep

Damage or clear enemies in an area.

### Stop the World

Temporarily freezes or heavily slows enemies.

### Compact

Pushes enemies away and/or attracts rewards.

These are design directions, not a mandatory final list unless a task explicitly asks for them.

The active ability pool should remain small enough for game-jam scope.

---

## 10. Passive Upgrade Direction

Passive upgrades should mostly modify existing values.

Possible examples:

- primary damage
- attack speed
- movement speed
- maximum HP
- pickup radius
- active cooldown
- projectile damage
- knockback

Programming-themed names are encouraged.

Possible naming ideas:

- Faster GC
- More Heap
- Compaction
- JIT
- Gen 2

Exact names and balance are not locked unless explicitly specified by a task.

---

## 11. Shop / Upgrade Screen

The player reaches a shop/upgrade screen after a stage.

Score/currency earned during gameplay is used for progression.

The shop may offer a small number of choices.

Passive upgrade:

```text
buy
  ->
apply immediately
```

Active ability:

```text
buy
  ->
choose active slot A or B
  ->
replace currently equipped ability
```

Example interaction:

```text
BUY: STOP THE WORLD

REPLACE?

> [A] DASH
  [B] COMPACT
```

Exact UX may change, but replacing active abilities must remain possible.

---

## 12. Score and Currency

A score system is mandatory.

The same value may also be used as shop currency if that remains the chosen design.

The score can be presented thematically as memory reclaimed or garbage collected.

Possible UI language:

```text
SCORE
FREED
BYTES
MEMORY CLEANED
```

The internal implementation should remain simple.

---

## 13. Health / Failure Theme

The player's health may be presented as memory/runtime health.

Possible thematic language:

- HEAP
- MEM
- FREE MEMORY

Game Over may be presented as:

```text
OUT OF MEMORY
PROCESS KILLED
```

This is thematic direction, not a strict final text requirement.

The game must nevertheless have a clear Game Over screen/state.

---

## 14. Win Theme

The final victory should communicate that memory has been cleaned and the main program can finally run.

Possible direction:

```text
MEMORY CLEAN
LAUNCHING MAIN...
HELLO, WORLD!
```

The exact final cinematic/text can change.

The game must nevertheless have a clear Win state / ending.

---

## 15. Stage Structure

Current target scope:

- at least 3 arenas/stages
- final boss
- progression between stages

Stages do not need completely separate engines or tile systems.

They may share the same arena systems with different:

- enemy mixes
- spawn patterns
- timing
- visual decorations
- stage names
- difficulty values

Possible thematic stage names:

- STACK
- HEAP
- RUNTIME

These names are suggestions, not locked content.

---

## 16. Enemy Scope

Current project scope targets approximately:

- 4 regular enemy types
- 1 final boss

Possible conceptual enemy directions:

### Garbage

Basic enemy that approaches the player.

### Leak

Slower or tougher pressure enemy related to memory leaks.

### Pointer

Fast or directional attacker.

### Exception

Ranged or disruptive enemy.

These concepts can change during design.

Do not consider this exact behavior locked unless a specific implementation task defines it.

---

## 17. Boss Direction

The final boss should strongly reinforce the runtime/memory theme.

Possible concepts:

- Memory Leak
- OOM
- Segfault
- Null
- corrupted process

Boss gameplay should remain achievable within Arduboy constraints.

A simple boss with readable patterns is preferred over a complex boss with many unique systems.

---

## 18. Visual Direction

Visual references:

- Circuit Dude
- WALL-E
- Baba Is You

Desired qualities:

- readable
- minimal
- monochrome
- expressive with few pixels
- text-friendly
- strong silhouettes
- playful programming aesthetic

The 128x64 monochrome display is part of the visual identity, not only a limitation.

---

## 19. Animation Scope

Current scope expects minimal animation.

A practical target:

- approximately 2 frames per character/enemy where animation is useful
- simple blinking/flipping/procedural effects elsewhere

Readability matters more than animation complexity.

---

## 20. Audio Scope

Desired audio:

- short gameplay sound effects
- player/enemy hit feedback
- ability feedback
- boss/victory feedback
- background melody if time and memory allow

Sound effects are higher priority than complex music.

---

## 21. Game Feel

The intended experience is:

- compact
- fast
- readable
- slightly chaotic
- satisfying to clean
- humorous/programmer-themed

The player should feel like they are actively cleaning a runtime that is becoming overwhelmed by garbage.

---

## 22. References

### Brotato

Reference for the core arena survival loop.

Relevant ideas:

- short combat stages
- survival pressure
- upgrades between stages
- compact arena
- build progression

### Vampire Survivors-like games

Reference for:

- automatic primary attacks
- enemy pressure
- build growth
- movement-focused combat

### Circuit Dude

Visual reference.

### WALL-E

Visual/character inspiration for the garbage-collector fantasy.

### Baba Is You

Reference for:

- minimal visual language
- readable text
- strong communication with simple graphics

These are inspirations, not requirements to copy specific mechanics or art.

---

## 23. Hardware-Driven Design

Arduboy constraints are part of the project challenge.

The game must be designed around:

- very limited SRAM
- limited flash
- 128x64 monochrome display
- limited buttons
- 8-bit CPU

The design should favor:

- small numbers of meaningful entities
- reused systems
- parameter variation
- compact UI
- cheap effects
- fixed-size data

Do not try to reproduce desktop Vampire Survivors scale literally.

The goal is to reproduce the feeling of pressure, survival and build growth within the platform.

---

## 24. Evaluation-Critical Requirements

The project is evaluated on:

- working game mechanics
- clear progression
- understandable rules
- technical stability/performance
- readable graphics/interface
- start and ending presentation
- theme/genre compliance
- sound effects

Important mandatory points:

- all controls must be used
- the game needs a start screen
- the game needs an ending state
- the game needs score
- the game needs Game Over
- the game needs Win

Critical gameplay bugs are especially costly.

For implementation decisions, a smaller stable feature is better than a larger unstable feature.

---

## 25. Current Scope Target

A reasonable current game-jam target is:

- 1 player character
- automatic primary weapon
- 2 active ability slots
- Dash as a starting active ability
- several additional active abilities
- several passive upgrades
- 4 regular enemy types
- 1 boss
- 3 or more stages
- shop between stages
- score/currency
- title screen
- Game Over
- Win ending
- essential sound effects

This is a target, not a reason to invent missing features without a user task.

---

## 26. Current Design Decisions

These decisions are currently considered intentional:

1. The game is an arena-survival / Vampire Survivors-like game.
2. The primary weapon is automatic.
3. The D-pad controls movement.
4. A and B are two active ability slots.
5. Dash is an active ability.
6. The player starts with Dash equipped.
7. Dash can later be replaced.
8. The player may end up with two non-Dash active abilities.
9. Upgrades/shop happen between stages.
10. Score/currency comes from combat progression.
11. The run ends in either Game Over or Win.
12. The final game should contain a boss.
13. The programming/runtime/GC theme should be visible throughout the game.

Do not contradict these decisions unless the current user task explicitly changes them.

---

## 27. Not Yet Locked

The following are intentionally not fixed here:

- exact player HP
- exact stage duration
- exact number of enemies on screen
- exact entity pool sizes
- exact Dash distance
- future changes to the starting Dash cooldown (currently 4 seconds)
- exact invulnerability duration
- exact active ability list
- exact passive upgrade list
- exact upgrade prices
- exact enemy stats
- exact boss attack pattern
- exact score formula
- exact final art
- exact final audio
- exact UI layout

These should be decided by future tasks, testing and memory/performance measurements.

Do not invent permanent values for them unless needed for the current implementation task.

If temporary values are necessary, keep them easy to tune and clearly named.

---

## 28. How to Use This File

For a feature task, the coding agent should:

1. Read `AGENTS.md`.
2. Read this file.
3. Treat the user's current task as the concrete requested change.
4. Preserve the current decisions above unless the task changes them.
5. Avoid implementing optional concepts that were not requested.

Example task:

```text
Implement active ability replacement in the shop.

Requirements:
- buying an active ability enters slot-selection mode;
- A replaces slot A;
- B replaces slot B;
- Dash can be replaced;
- cancelling the purchase must not spend score;
- build and report memory usage.
```

The task defines what to build now.

This file defines what game that task belongs to.

---

## 29. Agreed Arena and Combat Rules

These decisions govern the implemented test weapon as well as future combat.
Full enemy AI, damage to the player, stages, score and progression remain outside
the prototype scope described below.

- The arena occupies one screen; every point along all four edges wraps to the
  opposite edge. These are not isolated Pac-Man-style tunnels.
- Obstacles are sparse solid blocks and cover, not a maze. Stage layouts differ.
- The player and projectiles wrap. Enemies do not wrap; their entire hitbox stays
  inside the arena and pursuit uses direct, not cross-edge, directions.
- Walking slides along obstacles. Dash uses a fixed direction chosen at activation
  and stops on obstacle collision, retaining its cooldown. Wrapping does not stop it.
- The starting automatic weapon fires a moving projectile at the nearest target
  within range that has an unobstructed line of fire.
- Cover blocks ordinary player and enemy projectiles. Projectiles have bounded
  lifetimes so wrapping cannot keep them alive forever.
- A normal stage has a timed survival phase. When the timer expires, spawning
  stops, but the player must clear remaining enemies before entering the shop.
- Enemy obstacle handling must not leave inaccessible enemies that block cleanup.
- The prototype fixes a starter weapon only: 25-frame interval, 48-pixel range,
  2-pixel/frame projectile speed, 64-frame lifetime and 1 damage. Invulnerability
  duration, loot handling, other weapon parameters, enemy stats and enemy
  obstacle-avoidance behavior remain for later tasks.

## 30. Initial Combat-Test Prototype

The current implementation scope is deliberately smaller than the final game:

- Standard Arduboy / Arduboy FX, using Arduboy2 without external FX flash.
- Title and Playing states, one test layout with four static rectangles.
- 128x56 wrapping arena below an 8-pixel HUD; a 7x7 player.
- Integer fixed-point coordinates at 1/16 pixel, approximately normalized diagonal
  movement, bounded substeps to prevent Dash from skipping obstacles.
- Two symmetric ability slots. A initially contains Dash; B is empty and safely
  does nothing. A/B on the title both start without triggering an ability.
- Dash direction comes from current movement or last facing (initially right).
  Direction stays locked during the burst. Holding a button never repeats Dash.
- The user confirmed a 4-second Dash cooldown from `Механики_финал_финал.md`:
  250 frames at 16 ms/frame. Collision does not refund it.
- Temporary tuning: walk 1 pixel/frame, Dash 4 pixels/frame for 6 frames on an
  axis. Test combat uses three 3-HP dummies, automatic 25-frame shots, 48-pixel
  target range, 2-pixel/frame projectiles, four projectile slots and 64-frame
  projectile lifetime. These values remain easy to adjust in `config.h`.
- No score, damage to the player, stage transitions, shop, pause, audio, Game Over,
  Win or EEPROM high score in this prototype.

The wider mechanics/enemy notes are future design inputs. Do not implement their
contents implicitly or hardcode pause onto the B ability button.

## 31. Jam Limits and Verification

- Organizer limits: no more than 28 KB flash and 2 KB RAM, despite the MCU having
  32 KB flash and 2.5 KB RAM physically. Build checks currently interpret these as
  28 KiB and 2 KiB; report exact bytes so the interpretation remains visible.
- Source must be structured with comments explaining the main functions.
- EEPROM high-score storage is encouraged, not mandatory. Add it when score
  exists, preserve Arduboy's system EEPROM area, and avoid writes during combat.
- The prototype uses a stricter 1,400-byte static SRAM budget to leave room for
  stack/interrupts within the jam RAM limit. Static size is not peak runtime usage.
- Three dummies and four projectiles are test fixtures, not locked final enemy or
  projectile capacities.
- Player facing occupies one byte. Dummy HP (2 bits) and hit flash (3 bits) share
  one byte; use accessors instead of duplicating masks in gameplay/rendering.
  This saves 4 SRAM bytes without changing cooldowns or combat behavior.
- Source comments, including tests and build helpers, are in concise Russian for
  junior developers. Explain invariants and non-obvious math, not every assignment.
- Before field packing, an AVR harness measured only game-logic updates: worst observed
  update was 154,436 cycles (9.6523 ms at 16 MHz), with 118 bytes maximum observed
  update stack use. Rendering, display transfer, library work, interrupts and the
  physical console still need verification. These timing/stack numbers are historical;
  the packed layout has fresh build/test checks, but has not been reprofiled.
- Host gameplay tests and AVR build checks are separate from real-device testing.
  Physical input, display, frame timing, peak stack and upload/recovery must still
  be verified on a console. See README.md for measured build sizes and commands.

## 32. Wave-Clear Implementation (Current Task)

This section supersedes the historical prototype/timed-survival rules above.

- Reuse both existing stages and every wave in `src/stages.h`, including Basic,
  Fast and recursive Splitter enemies. No new boss or stage is invented here.
- Clear every wave to finish a stage. The 60-second bonus clock starts on entry,
  includes spawn warnings, uses exactly 3750 frames at 16 ms on AVR and host,
  and saturates at zero without ending combat or stopping later waves.
- Each cleared wave gives 50 score. Stage completion awards remaining whole
  seconds times 100 exactly once. Enemy rewards are pickups, with direct-score
  fallback when their pool is full; remaining drops are banked on stage clear.
- The final existing stage leads to Win, not a loop. GameOver and Win freeze
  gameplay; A/B returns to Title, and a fresh press starts a fully reset run.
- The arena is 104x64, wrapping in both axes. The old off-arena obstacle is moved
  to (20,32). The sidebar occupies x=104..127, not entity space.
- Sidebar rows at y=0,8,16,24,32,40 show level, four hearts, score, whole seconds,
  and A/B cooldown bars. Scores below 10000 are exact; higher values use integer
  thousands (10k, 11k, etc.). HP5/6 adds distinct markings to the first one/two
  hearts; lost health always has an empty outline.
- Start with HP4/maxHP4, Dash in A and empty B. Max HP is capped at 6. Contact
  damage is 1 with 60-frame invulnerability and four-frame blink phases. There
  is no contact knockback. Player movement, enemy pursuit and swarm separation
  reject overlap, including player contact across seams. Damage contact has a one-pixel
  movement tolerance so enemies stopped just outside the hitbox still hurt.
  Wave markers use deterministic, obstacle-free, in-bounds positions. If the
  player occupies any marker (including contact tolerance), the whole wave waits
  there, retrying every two frames with continued blinking. No spawn is relocated;
  the first visible frame stays exactly on the markers. Invalid direct spawns are
  rejected; splitter children inherit their parent's valid position.
- Each inter-stage shop has two sequential carousel screens: passive, then
  active. Each screen permits one purchase or a skip. Three fixed choices per
  category keep the current build predictable and remain stored in choice arrays
  for a larger future pool. Left/right cycles once per press; A buys/selects and
  B skips the current category. A passive purchase or skip opens active choices;
  an active purchase or skip starts the next stage. Active selection opens
  explicit A/B replacement, even with an empty slot; Left cancels without
  payment. Payment occurs only on successful purchase.
- Temporary prices: each passive costs 100 score, each active costs 200 score.
  Damage adds 1 per level (cap 3); max HP adds 1 and heals 1 (cap 6 HP);
  speed adds 2/16 pixel per frame per level (cap 3). Dash speed is unchanged.
- Temporary active effects: Sweep deals 4 damage within 24 pixels using wrapped
  distance (ignores cover), cooldown 180 frames; Freeze stops enemy movement and
  swarm separation for 90 frames, cooldown 250; Compact collects all current
  pickups and grants 60 shield frames, cooldown 200. All use normal A/B slots,
  with no placeholder purchases. Dash remains 6 frames with cooldown 250.
- ArduboyTones plays a looping menu theme in the main/submenus and shop. Enemy
  death, score-orb collection and player damage interrupt it with short SFX;
  menu music resumes afterwards. The existing saved sound toggle mutes all audio.
- No new dynamic allocation or unbounded pools. `make test` runs movement,
  live-wave/combat and drawing-spy regressions with address/undefined sanitizers.
  Movement-only fixtures isolate waves, never alter the timer or host constants.
  Drawing-spy tests check bounds/layout, not the physical display or real font.

## 33. Enemy and HUD Fixes

- Enemy movement, obstacle slides and swarm separation reject arena boundaries;
  no contact-resolution teleport is used. Player walking/Dash and projectiles
  retain wrapping, as do their contact/targeting checks where appropriate.
- F speed scales with lost HP from 4 to 13 fixed units per frame. The fastest
  movement vector is (13,6), below even base diagonal walking (11,11).
- All four 5x5 heart icons have two lobes, a top notch and a tapered point.
  HP5/6 adds a center cutout to the first one/two filled hearts; depleted hearts
  retain the empty outline. No extra sidebar space or persistent RAM is used.
