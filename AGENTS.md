# AGENTS.md

# Garbage Collector — Coding Agent Instructions

This file defines the persistent technical rules for coding agents working on this repository.

Before changing code:

1. Read this file.
2. Read `CONTEXT.md`.
3. Inspect the existing implementation.
4. Follow the current user task exactly.
5. Do not invent unrelated features or refactor unrelated systems.

The user provides implementation tasks separately. `CONTEXT.md` describes the game and current design decisions. This file describes how the project should be implemented safely on Arduboy-class hardware.

---

## 1. Primary Goal

Build a complete, stable and readable Arduboy game under severe hardware constraints.

Priority order:

1. The project must compile.
2. The game must run on real Arduboy / ATmega32u4-class hardware.
3. SRAM usage must remain safe.
4. Core gameplay must remain functional.
5. Required controls and game states must remain functional.
6. Performance should remain stable.
7. Features and polish come after stability.

Prefer simple, bounded and predictable code over generic or abstract architecture.

---

## 2. Target Platform

Target platform:

- Arduboy / Arduino Leonardo compatible hardware
- MCU: ATmega32u4
- 8-bit AVR
- approximately 32 KB flash
- approximately 2.5 KB SRAM
- approximately 1 KB EEPROM
- 128x64 monochrome display

The framebuffer already consumes a large part of available SRAM.

Treat RAM as the most constrained resource.

Do not optimize only for emulator execution.

---

## 3. Hard Memory Rules

Avoid dynamic memory.

Do not use:

- `new`
- `delete`
- `malloc`
- `free`
- Arduino `String`
- dynamically growing containers
- `std::vector`
- `std::list`
- `std::map`
- recursion

Prefer:

- fixed-size arrays
- fixed-size object pools
- static storage
- small local variables
- `PROGMEM` for immutable data
- compile-time limits
- small integer types

Prefer these when their ranges are sufficient:

- `uint8_t`
- `int8_t`
- `uint16_t`
- `int16_t`

Do not use wider types without a reason.

Before adding persistent fields to a struct, consider their SRAM cost.

---

## 4. Bounded Entity Storage

All gameplay entity collections must have fixed maximum sizes.

This includes:

- enemies
- enemy bullets
- player projectiles
- pickups
- visual effects
- temporary gameplay objects

Use fixed arrays or pools.

Example:

```cpp
constexpr uint8_t MAX_ENEMIES = 12;

struct Enemy {
    int8_t x;
    int8_t y;
    uint8_t hp;
    uint8_t type;
    uint8_t state;
    bool active;
};

Enemy enemies[MAX_ENEMIES];
```

The exact pool sizes are not sacred. They should be chosen from measured memory usage and gameplay needs.

If a pool is full, fail gracefully.

Acceptable fallback behavior:

- skip a non-critical spawn;
- reuse an inactive slot;
- merge rewards;
- award score directly instead of spawning an extra pickup;
- delay a spawn.

Never write outside array bounds.

Never allocate extra heap memory as a fallback.

---

## 5. High-Level Game State

Use an explicit high-level state machine.

Expected states may include:

```cpp
enum class GameState : uint8_t {
    Title,
    StageIntro,
    Playing,
    Shop,
    BossIntro,
    GameOver,
    Win
};
```

Only systems relevant to the current state should update.

Gameplay must not continue behind:

- title screen
- shop
- Game Over
- Win
- transition screens

Do not duplicate ownership of global game state.

---

## 6. Controls and Active Ability Slots

Controls:

- D-pad: movement
- A: activate ability equipped in active slot A
- B: activate ability equipped in active slot B

A and B are ability slots, not hardcoded actions.

The player starts a run with Dash equipped in one active slot.

Dash is a normal replaceable active ability.

The game must support builds such as:

- Dash + another active ability
- another active ability + Dash
- two non-Dash active abilities

Do not assume Dash is always available later in the run.

Do not hardcode Dash directly into A-button or B-button handling.

Input should conceptually follow:

```text
button press
    ->
active slot
    ->
equipped ability
    ->
ability activation
```

Example:

```cpp
if (arduboy.justPressed(A_BUTTON)) {
    activateAbility(player.activeA);
}

if (arduboy.justPressed(B_BUTTON)) {
    activateAbility(player.activeB);
}
```

One-shot abilities must use edge-triggered input unless a specific ability explicitly supports held input.

Both A and B must remain meaningfully usable by the game.

---

## 7. Active Ability Model

Prefer a compact representation.

Example:

```cpp
enum class AbilityId : uint8_t {
    None,
    Dash,
    MarkAndSweep,
    StopTheWorld,
    Compact
};
```

A lightweight player representation is preferred:

```cpp
AbilityId activeA;
AbilityId activeB;

uint8_t cooldownA;
uint8_t cooldownB;
```

A larger ability object hierarchy is not necessary.

Prefer dispatch by `AbilityId`.

Example:

```cpp
void activateAbility(AbilityId ability);
```

or equivalent logic that fits the existing architecture.

Cooldowns should normally use small frame counters.

---

## 8. Dash

Dash is an active ability.

It is not a permanent player movement subsystem tied to a specific button.

Preferred properties:

- movement burst
- cooldown
- optional short invulnerability window
- direction based on current movement input or facing direction
- no new dynamic entity

Dash should reuse the same ability activation and cooldown infrastructure as other active abilities where practical.

Do not special-case Dash in input handling.

---

## 9. Upgrade and Shop Rules

Upgrades may be:

- passive upgrades
- active abilities

Passive upgrades should usually modify existing parameters.

Good examples:

- damage
- attack speed
- movement speed
- max HP
- pickup range
- active cooldown
- projectile damage
- knockback

Active ability purchases may require replacing one of the two equipped active slots.

The implementation should support selecting slot A or slot B when replacement is required.

Do not build a large inventory system unless explicitly requested.

Prefer data-driven upgrade definitions when practical.

---

## 10. Player Attack

The primary weapon should be automatic unless the current task explicitly changes this design.

The player should not need an extra fire button.

Prefer cheap attacks that avoid persistent projectiles when projectile movement is not important.

Example:

1. find a target;
2. apply damage;
3. create a short visual effect;
4. start attack cooldown.

Real projectiles are acceptable when their movement is part of gameplay.

---

## 11. Movement and Math

Avoid floating point in normal gameplay.

Avoid:

- `float`
- `double`
- `sqrt`
- `pow`
- `sin`
- `cos`

unless clearly justified.

Prefer:

- integer coordinates
- integer arithmetic
- fixed-point math when sub-pixel precision is needed
- squared-distance checks
- Manhattan distance
- lookup tables

Example squared distance:

```cpp
int16_t dx = enemy.x - player.x;
int16_t dy = enemy.y - player.y;
uint16_t dist2 = dx * dx + dy * dy;

if (dist2 <= radiusSquared) {
    // collision
}
```

Do not introduce expensive math casually.

---

## 12. Collision Rules

Prefer simple collision methods:

- AABB
- radius approximation
- squared-distance checks

Avoid pixel-perfect collision.

Gameplay hitboxes may be simpler than sprite shapes.

Fairness and readability are more important than geometric precision.

---

## 13. Enemy Architecture

Enemy logic should remain cheap.

Prefer:

- move toward player
- move on one axis
- timed charge
- timed shot
- simple orbit
- small finite-state machines

Avoid:

- pathfinding
- navigation meshes
- complex steering
- behavior trees
- physics engines
- deep class hierarchies

Prefer compact structs with a type/state field.

Example:

```cpp
enum class EnemyType : uint8_t {
    Garbage,
    Leak,
    Pointer,
    Exception,
    Boss
};
```

Shared storage plus type-based behavior is preferred over one class per enemy type.

---

## 14. Boss Architecture

Bosses should use explicit finite-state behavior.

Example:

```text
MOVE
WAIT
ATTACK
SPAWN
RECOVER
```

Boss phases should preferably alter parameters or existing patterns:

- speed
- cooldown
- projectile count
- spawn interval
- damage

Avoid creating an entirely new subsystem for every boss phase.

---

## 15. Rendering

Display target:

```text
128 x 64
1-bit monochrome
```

Gameplay readability has priority over sprite detail.

Prefer:

- clear silhouettes
- small sprites
- high contrast
- minimal HUD
- very limited animation
- cheap procedural effects

Store immutable sprites and bitmap data in flash / `PROGMEM`.

Do not keep unnecessary full bitmap copies in SRAM.

Avoid additional full-screen buffers.

---

## 16. Animation and Effects

Keep animation lightweight.

Prefer:

- 2-frame animation
- blinking
- sprite flipping
- simple counters
- short-lived lines
- flashes
- strict small effect pools

Do not build a heavyweight generic animation framework unless the project clearly needs one.

Visual effects must not threaten frame rate or SRAM.

---

## 17. Audio

Audio must not block gameplay.

Prefer short and inexpensive effects.

Priority:

1. gameplay feedback
2. enemy/player hit feedback
3. ability feedback
4. shop/menu feedback
5. boss/win feedback
6. background music

If music threatens memory, timing or stability, simplify music before removing core gameplay.

Avoid blocking `delay()` calls in active gameplay.

---

## 18. Timing

Prefer frame counters.

Example:

```cpp
if (cooldownA > 0) {
    --cooldownA;
}
```

Use a deterministic frame-based model when possible.

Avoid blocking delays during gameplay.

Transition screens should also preferably use non-blocking timers.

---

## 19. Randomness

Random logic must be bounded.

Good uses:

- spawn positions
- upgrade choices
- minor enemy variation

Avoid uncontrolled loops that search indefinitely for a valid result.

If retry logic is needed, give it a strict maximum number of attempts.

All random logic must terminate predictably.

---

## 20. Score and Pickups

The game must keep a visible score system.

Score may also be used as upgrade currency if the current design says so.

Pickup entities must use bounded storage.

If pickup storage is full, rewards should degrade safely, for example by awarding score directly.

Do not allow reward handling to corrupt memory or silently break progression.

---

## 21. Code Organization

Inspect the repository before deciding structure.

A possible organization is:

```text
src/
  main.cpp
  game.cpp
  game.h
  player.cpp
  player.h
  enemies.cpp
  enemies.h
  abilities.cpp
  abilities.h
  upgrades.cpp
  upgrades.h
  projectiles.cpp
  projectiles.h
  rendering.cpp
  rendering.h
  audio.cpp
  audio.h
  assets.h
```

This is a guideline, not a requirement.

Do not reorganize working code without a concrete reason.

For a game jam, a small simple codebase is preferable to architecture for architecture's sake.

---

## 22. Coding Style

Use English identifiers.

Prefer clear names such as:

```cpp
playerHp
enemyCount
activeA
activeB
spawnEnemy()
updatePlayer()
activateAbility()
drawEnemy()
```

Use named constants for gameplay values.

Example:

```cpp
constexpr uint8_t PLAYER_MAX_HP = 3;
constexpr uint8_t DASH_COOLDOWN = 30;
```

Comments may be English or Russian.

Keep functions focused and understandable.

Avoid unnecessary generic utility layers.

---

## 23. Data-Oriented Preference

Prefer simple data plus functions over deep inheritance.

Avoid designs such as:

```text
Entity
 └ Character
    └ Enemy
       └ SpecialEnemy
          └ BossEnemy
```

Prefer compact structs and type/state fields.

The goal is:

- predictable memory use
- easy debugging
- easy serialization of state
- cheap iteration
- low code complexity

---

## 24. Frame Update

A reasonable conceptual order is:

```text
read input
update current game state
update player
update abilities
update enemies
update projectiles
resolve collisions
process deaths
update score/rewards
process spawning
render
```

The exact order may differ if existing code requires it.

Be careful when modifying entity pools during iteration.

Prefer marking objects inactive and reusing slots.

---

## 25. Resource Ownership

Each system should have clear ownership.

Examples:

- player system owns player state
- ability system owns ability behavior
- enemy system owns enemy pool
- projectile system owns projectile pool
- game system owns stage and high-level state
- upgrade system owns acquired upgrades

Avoid duplicate copies of:

- player HP
- score
- stage
- active abilities
- cooldowns

---

## 26. Graceful Failure

Under resource pressure, the game should degrade safely.

Examples:

- enemy pool full -> skip optional spawn
- projectile pool full -> skip optional projectile
- pickup pool full -> award score directly
- effect pool full -> omit effect

Never let optional presentation corrupt core gameplay.

---

## 27. Dependencies

Do not add third-party libraries casually.

Before adding a dependency, consider:

- flash cost
- SRAM cost
- AVR compatibility
- Arduboy compatibility
- whether the functionality is simple enough to implement locally

Do not introduce desktop-oriented libraries.

---

## 28. Refactoring Rules

Do not perform broad refactors unless:

- explicitly requested
- required by the requested feature
- necessary to fix a serious structural problem

Avoid rewriting working systems while implementing an unrelated feature.

Prefer small focused diffs.

Preserve the existing build system.

---

## 29. Feature Implementation Workflow

For every user task:

1. Read `AGENTS.md`.
2. Read `CONTEXT.md`.
3. Inspect related source files.
4. Identify affected systems.
5. Reuse existing systems where reasonable.
6. Estimate RAM/flash impact.
7. Implement the smallest correct version.
8. Preserve unrelated behavior.
9. Build the project.
10. Fix compile errors caused by the change.
11. Report what changed and what was actually verified.

Do not implement unrelated TODO items.

Do not silently change gameplay design.

If a requested feature conflicts with hardware limitations, explain the conflict and prefer the cheapest equivalent implementation.

---

## 30. Testing Expectations

After gameplay changes, verify relevant flows.

Minimum:

```text
TITLE -> PLAYING
```

```text
PLAYING -> GAME OVER
```

When stage logic changes:

```text
PLAYING -> STAGE COMPLETE -> SHOP -> NEXT STAGE
```

When final-stage logic changes:

```text
FINAL STAGE -> WIN
```

When input or abilities change:

- D-pad still moves the player
- A activates slot A
- B activates slot B
- Dash works when equipped
- replacing Dash does not leave hidden Dash behavior behind
- empty slots behave safely if empty slots are allowed

When entity handling changes:

- full pools are handled safely
- entity death is safe
- repeated spawning is safe
- no obvious out-of-bounds writes exist

---

## 31. Build Verification

Do not claim a successful implementation without build verification when build tools are available.

After changes:

- compile the project
- report compiler errors
- report relevant warnings
- report flash/RAM usage when available

If the project cannot be built in the current environment, say so clearly.

Never fabricate build or memory results.

---

## 32. Definition of Done

A feature is complete when:

- requested behavior is implemented
- the project compiles when build tools are available
- core gameplay remains functional
- no dynamic allocation was introduced
- entity storage remains bounded
- no obvious out-of-bounds access was introduced
- A/B ability slots still behave correctly
- the feature fits Arduboy constraints
- unrelated systems were not unnecessarily rewritten

---

## 33. Agent Report Format

After implementing a task, report briefly:

```text
Implemented:
- ...

Files changed:
- ...

Technical notes:
- ...

Build:
- success / failure / not available

Memory:
- flash: ...
- SRAM: ...
```

Only report memory numbers that actually came from a build/tool.

State assumptions.

State unresolved issues instead of hiding them.

---

## 34. Do Not

Do not:

- use dynamic allocation
- use Arduino `String`
- use dynamic STL containers
- use floating point without a good reason
- create unbounded entity collections
- introduce deep inheritance
- add heavyweight frameworks
- add blocking gameplay delays
- write outside fixed arrays
- hardcode Dash to A or B
- assume Dash always exists after the start of a run
- silently ignore requested behavior
- implement unrelated features
- rewrite working code unnecessarily
- sacrifice game stability for visual polish
- assume emulator-only behavior is sufficient
- invent build or test results

---

## 35. Source of Truth

Use the following precedence:

1. Hardware safety and actual platform limits.
2. The user's current explicit task.
3. `CONTEXT.md` for current game/product decisions.
4. `AGENTS.md` for implementation constraints.
5. Existing working behavior where it does not conflict with the above.

If sources conflict, do not silently guess.

Mention the conflict and choose the safest minimal implementation.

---

## 36. Guiding Principle

This is a constrained 8-bit game jam project.

Prefer:

- simple
- bounded
- predictable
- readable
- measurable
- working

over:

- generic
- dynamic
- abstract
- large
- clever

The final goal is a complete, stable and enjoyable Arduboy game.
