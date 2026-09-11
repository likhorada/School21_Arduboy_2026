# 🗑️ Garbage Collector

> *🇷🇺 [Русская версия](README.md)*

**A garbage collector cleans up memory trash to make room for running the main program.**

---

## 📌 Overview

| | |
|---|---|
| **Genre** | Roguelike, Arena Shooter, Bullet Hell |
| **Platform** | Arduboy |
| **Target audience** | Arduboy fans |

---

## 🎮 Core Gameplay

The loop is built on three pillars: **action → reward → progression**.

1. The Garbage Collector destroys enemies while moving around the arena and dodging their attacks.
2. Enemies drop points when defeated.
3. After clearing an arena, an upgrade shop opens where earned points can be spent.
4. The (un)upgraded Garbage Collector moves on to the next arena — until the final one is cleared.

---

## 🎨 References

| Category | Reference |
|---|---|
| Gameplay | **Brotato** — core gameplay loop |
| Visual | **Circuit Dude** |
| Visual | **WALL-E** |
| Visual / text-based | **Baba Is You** |

---

## 💭 Player emotions / experience

> *"Clean, clean, clean it with a fork"*
> — a quote from an underground film

---

## ⚙️ Project constraints

The **Arduboy** platform is a challenge in itself:

- strict technical memory limitations for the programmer;
- limited visual style (monochrome, low-resolution screen) for the artist.

---

## 📋 Estimated scope

### 🖌️ Visual

- **Screens & UX:**
  - start screen
  - intermediate screens
  - final cinematic
  - arenas (at least 3)
- **Characters & enemies:**
  - 2 png per character/mob (for animation)
  - 4 mobs + final boss

### 🔊 Audio

- background music
- "non-annoying" attack/damage sounds — for both the character and the mobs/boss

### 💻 Development

- game rendering
- solving memory issues (inevitable)
- character coordinate calculations
- boss movement/attack logic
- sound implementation

---