# Beat 'em Up Game - Attack System Implementation

## Overview
This document describes the implementation of the attack system for the beat 'em up game, fulfilling the requirements outlined in the project description.

## Implemented Features

### 1. Attack System
The game now includes two types of attacks that can be triggered with keyboard buttons:

#### a. Light Attack (X key)
- **Duration**: 0.3 seconds (faster)
- **Animation Frames**: 15 frames
- **Sprite Used**: hand_long_attack (sprite #3 from player_sprite.bmp)
- **Behavior**: Quick attack that completes rapidly

#### b. Heavy Attack (Y key)
- **Duration**: 0.6 seconds (slower)
- **Animation Frames**: 30 frames
- **Sprite Used**: stomach_attack (sprite #4 from player_sprite.bmp)
- **Behavior**: Slower, more powerful attack

### 2. Character Control and Movement
The character movement system has been enhanced with the following features:

- **Movement Prevention During Attacks**: Players cannot move while performing an attack
- **Smooth Control**: Character parameters (PLAYER_SPEED, attack durations) are defined as constants and can be easily adjusted
- **State-Based Control**: The game uses a state machine (STATE_IDLE, STATE_LIGHT_ATTACK, STATE_HEAVY_ATTACK) for smooth transitions

### 3. Sprite Replacement System
The implementation includes a dynamic sprite selection system:

- **Fixed Frame Duration**: Each attack displays its specific sprite for a fixed number of frames
- **Automatic Transitions**: After the attack duration expires, the character automatically returns to the idle state
- **Sprite Mapping**: Different sprites are selected based on the current player state:
  - Idle: standing sprite (sprite #0)
  - Light Attack: hand_long_attack sprite (sprite #3)
  - Heavy Attack: stomach_attack sprite (sprite #4)

### 4. Sprite Sheet Organization
The first row of `player_sprite.bmp` contains 8 sprites (46x50 pixels each):
1. standing (idle)
2. attack_crouch
3. attack_fist_defense
4. hand_long_attack (used for light attack)
5. stomach_attack (used for heavy attack)
6. stomach_attack_prepare
7. jump_forward
8. jump_landing

## Technical Implementation

### New Constants
```cpp
#define LIGHT_ATTACK_FRAMES 15
#define HEAVY_ATTACK_FRAMES 30
#define LIGHT_ATTACK_DURATION 0.3
#define HEAVY_ATTACK_DURATION 0.6
```

### Player State Enum
```cpp
enum PlayerState {
  STATE_IDLE = 0,
  STATE_LIGHT_ATTACK = 1,
  STATE_HEAVY_ATTACK = 2
};
```

### GameState Structure
Enhanced with attack-related fields:
```cpp
struct GameState {
  // ... existing fields ...
  PlayerState state;
  double attackTimer;
  int animationFrame;
};
```

### Key Functions Added

1. **UpdateAttackState(GameState *game, double delta)**
   - Updates the attack timer and animation frame
   - Transitions back to idle state when attack completes

2. **StartLightAttack(GameState *game)**
   - Initiates a light attack if the player is idle

3. **StartHeavyAttack(GameState *game)**
   - Initiates a heavy attack if the player is idle

4. **DrawPlayer** (modified)
   - Now accepts PlayerState parameter
   - Selects appropriate sprite based on current state

5. **UpdatePlayerPosition** (modified)
   - Only allows movement when in STATE_IDLE
   - Prevents movement during attacks

## Controls

| Key | Action |
|-----|--------|
| W/A/S/D | Move character (up/left/down/right) |
| X | Light Attack (fast) |
| Y | Heavy Attack (slow) |
| N | New Game |
| ESC | Exit Game |

## Adjustable Parameters

The following parameters can be easily modified to tune gameplay:

- **PLAYER_SPEED**: Movement speed (currently 150.0)
- **LIGHT_ATTACK_DURATION**: Duration of light attack (currently 0.3s)
- **HEAVY_ATTACK_DURATION**: Duration of heavy attack (currently 0.6s)
- **LIGHT_ATTACK_FRAMES**: Animation frames for light attack (currently 15)
- **HEAVY_ATTACK_FRAMES**: Animation frames for heavy attack (currently 30)

## Future Enhancements

Possible extensions to this implementation:
- Add combo system for chaining attacks
- Implement damage calculation for enemies
- Add more attack types (crouch attack, jump attack)
- Include attack animations with multiple sprite frames
- Add sound effects for attacks
- Implement attack cooldowns
- Add directional attacks based on movement direction
