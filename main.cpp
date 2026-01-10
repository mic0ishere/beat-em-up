#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

extern "C" {
#if defined(__aarch64__) || defined(_M_ARM64)
#include "/opt/homebrew/opt/sdl2/include/SDL2/SDL.h"
#include "/opt/homebrew/opt/sdl2/include/SDL2/SDL_main.h"
#else
#define SDL_MAIN_HANDLED
#include "./SDL2-2.0.10/include/SDL.h"
#include "./SDL2-2.0.10/include/SDL_main.h"
#endif
}

#define SCREEN_WIDTH 1000
#define SCREEN_HEIGHT 600
#define STAGE_WIDTH 1600
#define FLOOR_HEIGHT 200
#define PLAYER_SPEED 150.0
#define CAMERA_SMOOTH_FACTOR 5.0

#define CLOUD_TEXTURE_WIDTH 256
#define CLOUD_TEXTURE_HEIGHT 144
#define FLOOR_TILE_SIZE 48
#define FLOOR_TILES_HORIZONTAL ((STAGE_WIDTH / FLOOR_TILE_SIZE) + 2)
#define FLOOR_TILES_VERTICAL ((FLOOR_HEIGHT / FLOOR_TILE_SIZE) + 1)

// Depth scaling parameters
#define DEPTH_SCALE_MIN 0.7  // minimum scale (at bottom of screen)
#define DEPTH_SCALE_MAX 1.0  // maximum scale (at top of play area)

#define FONT_SIZE 10
#define FONT_SRC_WIDTH 20
#define FONT_SRC_HEIGHT 26

#define SPRITE_SRC_WIDTH 46
#define SPRITE_SRC_HEIGHT 50
#define SPRITE_WIDTH 64
#define SPRITE_HEIGHT                                                          \
  ((int)(SPRITE_SRC_HEIGHT * ((double)SPRITE_WIDTH / SPRITE_SRC_WIDTH)))

#define CROUCH_DEFENSE_DURATION 0.3
#define HEAVY_ATTACK_DURATION 0.6

#define INPUT_BUFFER_SIZE 10
#define INPUT_TIMEOUT 0.8 // seconds before input expires
#define DASH_DURATION 0.3
#define TRIPLE_ATTACK_DURATION 1.5
#define COMBO_SEQUENCE_DURATION 2.0
#define DASH_TAP_THRESHOLD 0.4 // max time for a key press to count as a tap

#define DEBUG_MENU_START 70

enum InputType {
  INPUT_NONE = 0,
  INPUT_LIGHT = 1, // R key
  INPUT_HEAVY = 2, // F key
  INPUT_UP = 3,
  INPUT_DOWN = 4,
  INPUT_LEFT = 5,
  INPUT_RIGHT = 6
};

enum PlayerState {
  STATE_IDLE = 0,
  STATE_CROUCH_DEFENSE = 1,
  STATE_HEAVY_ATTACK = 2,
  STATE_TRIPLE_LIGHT_ATTACK = 3, // Triple R press
  STATE_TRIPLE_HEAVY_ATTACK = 4, // Triple F press
  STATE_COMBO_SEQUENCE = 5,      // R-F-R sequence
  STATE_DASH = 6                 // Quick movement dash
};

// Structure to hold input buffer entries
struct InputEntry {
  InputType type;
  double timestamp;
};

// Structure for combo definitions - makes it easy to add new combos
struct ComboDefinition {
  InputType sequence[5]; // Max 5 inputs in a combo
  int length;
  PlayerState resultState;
  double duration;
  const char *name;
};

struct GameState {
  double playerX, playerY;
  double cameraX;
  double worldTime;
  int moveLeft, moveRight, moveUp, moveDown;
  PlayerState state;
  double attackTimer;

  InputEntry inputBuffer[INPUT_BUFFER_SIZE];
  int bufferCount;
  double lastInputTime;
  int debugMenuEnabled;
  char currentActionName[64];

  // Track key press times for tap detection
  double keyPressTime[4]; // W, A, S, D (up, left, down, right)
  
  // Floor tile pattern - stores which tile (0 or 1) to use at each position
  int floorPattern[FLOOR_TILES_VERTICAL * FLOOR_TILES_HORIZONTAL];
};

void DrawString(SDL_Surface *screen, int x, int y, const char *text,
                SDL_Surface *charset) {

  SDL_Rect source, dest;
  source.w = FONT_SRC_WIDTH;
  source.h = 26;
  dest.w = FONT_SIZE;
  dest.h = FONT_SIZE;

  while (*text) {
    int character = (*text & 255) - 32;

    // clamp characters to bitmap range (32-127)
    if (character < 0 || character >= 96) {
      character = 31; // replace unknown characters with '?'
    }

    source.x = (character % 16) * FONT_SRC_WIDTH;
    source.y = (character / 16) * FONT_SRC_HEIGHT;
    dest.x = x;
    dest.y = y;
    SDL_BlitScaled(charset, &source, screen, &dest);
    x += FONT_SIZE;
    text++;
  }
}

void DrawPixel(SDL_Surface *surface, int x, int y, Uint32 color) {
  int bytesPerPixel = surface->format->BytesPerPixel;
  Uint8 *pixel =
      (Uint8 *)surface->pixels + y * surface->pitch + x * bytesPerPixel;
  *(Uint32 *)pixel = color;
}
void DrawLine(SDL_Surface *screen, int x, int y, int length, int dx, int dy,
              Uint32 color) {
  for (int i = 0; i < length; i++) {
    DrawPixel(screen, x, y, color);
    x += dx;
    y += dy;
  }
}

void DrawRectangle(SDL_Surface *screen, int x, int y, int width, int height,
                   Uint32 outlineColor, Uint32 fillColor) {
  DrawLine(screen, x, y, height, 0, 1, outlineColor);
  DrawLine(screen, x + width - 1, y, height, 0, 1, outlineColor);
  DrawLine(screen, x, y, width, 1, 0, outlineColor);
  DrawLine(screen, x, y + height - 1, width, 1, 0, outlineColor);

  for (int i = y + 1; i < y + height - 1; i++) {
    DrawLine(screen, x + 1, i, width - 2, 1, 0, fillColor);
  }
}

void InitGame(GameState *game) {
  game->playerX = 100;
  game->playerY = 0;
  game->cameraX = 0;
  game->worldTime = 0;
  game->moveLeft = 0;
  game->moveRight = 0;
  game->moveUp = 0;
  game->moveDown = 0;
  game->state = STATE_IDLE;
  game->attackTimer = 0;
  game->bufferCount = 0;
  game->lastInputTime = 0;
  game->debugMenuEnabled = 0;
  strcpy(game->currentActionName, "idle");
  // Initialize key press times
  for (int i = 0; i < 4; i++) {
    game->keyPressTime[i] = 0;
  }
  
  // Initialize floor pattern with random tiles
  for (int i = 0; i < FLOOR_TILES_VERTICAL * FLOOR_TILES_HORIZONTAL; i++) {
    game->floorPattern[i] = rand() % 2;  // randomly choose tile 0 or 1
  }
}

// Define all combos - easy to add new ones by adding entries here
ComboDefinition combos[] = {
    // Triple light attack (R-R-R)
    {{INPUT_LIGHT, INPUT_LIGHT, INPUT_LIGHT},
     3,
     STATE_TRIPLE_LIGHT_ATTACK,
     TRIPLE_ATTACK_DURATION,
     "triple light"},
    // Triple heavy attack (F-F-F)
    {{INPUT_HEAVY, INPUT_HEAVY, INPUT_HEAVY},
     3,
     STATE_TRIPLE_HEAVY_ATTACK,
     TRIPLE_ATTACK_DURATION,
     "triple heavy"},
    // Combo sequence (R-F-R)
    {{INPUT_LIGHT, INPUT_HEAVY, INPUT_LIGHT},
     3,
     STATE_COMBO_SEQUENCE,
     COMBO_SEQUENCE_DURATION,
     "combo sequence"},
    // Dash combos - double tap movement keys
    {{INPUT_LEFT, INPUT_LEFT}, 2, STATE_DASH, DASH_DURATION, "dash left"},
    {{INPUT_RIGHT, INPUT_RIGHT}, 2, STATE_DASH, DASH_DURATION, "dash right"},
    {{INPUT_UP, INPUT_UP}, 2, STATE_DASH, DASH_DURATION, "dash up"},
    {{INPUT_DOWN, INPUT_DOWN}, 2, STATE_DASH, DASH_DURATION, "dash down"}};
const int comboCount = sizeof(combos) / sizeof(ComboDefinition);

// Add input to buffer
void AddInput(GameState *game, InputType input) {
  if (game->bufferCount >= INPUT_BUFFER_SIZE) {
    // Shift buffer left to make room
    for (int i = 0; i < INPUT_BUFFER_SIZE - 1; i++) {
      game->inputBuffer[i] = game->inputBuffer[i + 1];
    }
    game->bufferCount = INPUT_BUFFER_SIZE - 1;
  }

  game->inputBuffer[game->bufferCount].type = input;
  game->inputBuffer[game->bufferCount].timestamp = game->worldTime;
  game->bufferCount++;
  game->lastInputTime = game->worldTime;
}

// Clean expired inputs from buffer
void CleanInputBuffer(GameState *game) {
  int writeIdx = 0;
  for (int i = 0; i < game->bufferCount; i++) {
    if (game->worldTime - game->inputBuffer[i].timestamp < INPUT_TIMEOUT) {
      if (writeIdx != i) {
        game->inputBuffer[writeIdx] = game->inputBuffer[i];
      }
      writeIdx++;
    }
  }
  game->bufferCount = writeIdx;
}

// Check if buffer matches a combo sequence
bool MatchesCombo(GameState *game, const ComboDefinition *combo) {
  if (game->bufferCount < combo->length)
    return false;

  // Check last N inputs match the combo
  int startIdx = game->bufferCount - combo->length;
  for (int i = 0; i < combo->length; i++) {
    if (game->inputBuffer[startIdx + i].type != combo->sequence[i]) {
      return false;
    }
  }
  return true;
}

// Check for combo matches and execute
void CheckCombos(GameState *game) {
  // Allow interrupt of basic attacks with combos, but not combo attacks
  bool canInterrupt =
      (game->state == STATE_IDLE || game->state == STATE_CROUCH_DEFENSE ||
       game->state == STATE_HEAVY_ATTACK);
  if (!canInterrupt)
    return;

  for (int i = 0; i < comboCount; i++) {
    if (MatchesCombo(game, &combos[i])) {
      game->state = combos[i].resultState;
      game->attackTimer = combos[i].duration;
      strcpy(game->currentActionName, combos[i].name);
      // Clear buffer after successful combo
      game->bufferCount = 0;
      return;
    }
  }
}

void DrawSkyGradient(SDL_Surface *screen) {
  for (int y = 0; y < SCREEN_HEIGHT - FLOOR_HEIGHT; y++) {
    // todo: improve gradient calculation
    int blue = 0x33 + (0x88 - 0x33) * y / SCREEN_HEIGHT - FLOOR_HEIGHT;
    Uint32 skyColor = SDL_MapRGB(screen->format, 0x66, 0x99, blue);
    DrawLine(screen, 0, y, SCREEN_WIDTH, 1, 0, skyColor);
  }
}

void DrawFloor(SDL_Surface *screen, Uint32 brown) {
  for (int y = SCREEN_HEIGHT - FLOOR_HEIGHT; y < SCREEN_HEIGHT; y++) {
    DrawLine(screen, 0, y, SCREEN_WIDTH, 1, 0, brown);
  }
}

void DrawStage(SDL_Surface *screen, double cameraX, SDL_Surface *cloudTexture, 
               SDL_Surface *floorTexture, GameState *game) {
  // Draw repeating cloud background
  if (cloudTexture != NULL) {
    int skyHeight = SCREEN_HEIGHT - FLOOR_HEIGHT;
    
    // Calculate horizontal offset for parallax effect (clouds move slower than camera)
    int cloudOffsetX = (int)(cameraX * 0.3) % CLOUD_TEXTURE_WIDTH;
    
    // Draw clouds repeatedly across the sky
    for (int x = -cloudOffsetX; x < SCREEN_WIDTH; x += CLOUD_TEXTURE_WIDTH) {
      for (int y = 0; y < skyHeight; y += CLOUD_TEXTURE_HEIGHT) {
        SDL_Rect dest;
        dest.x = x;
        dest.y = y;
        dest.w = CLOUD_TEXTURE_WIDTH;
        dest.h = CLOUD_TEXTURE_HEIGHT;
        SDL_BlitSurface(cloudTexture, NULL, screen, &dest);
      }
    }
  } else {
    // Fallback to gradient if texture not loaded
    DrawSkyGradient(screen);
  }
  
  // Draw floor with tiled texture
  if (floorTexture != NULL) {
    int floorStartY = SCREEN_HEIGHT - FLOOR_HEIGHT;
    
    // Calculate which tiles are visible
    int startTileX = (int)(cameraX / FLOOR_TILE_SIZE);
    if (startTileX < 0) startTileX = 0;
    int endTileX = startTileX + (SCREEN_WIDTH / FLOOR_TILE_SIZE) + 2;
    if (endTileX > FLOOR_TILES_HORIZONTAL) endTileX = FLOOR_TILES_HORIZONTAL;
    
    for (int tileY = 0; tileY < FLOOR_TILES_VERTICAL; tileY++) {
      for (int tileX = startTileX; tileX < endTileX; tileX++) {
        // Get the tile type from the persistent pattern
        int patternIndex = tileY * FLOOR_TILES_HORIZONTAL + tileX;
        int tileType = game->floorPattern[patternIndex];
        
        // Calculate source rectangle (which tile from texture to use)
        SDL_Rect source;
        source.x = tileType * FLOOR_TILE_SIZE;  // tile 0 or tile 1
        source.y = 0;
        source.w = FLOOR_TILE_SIZE;
        source.h = FLOOR_TILE_SIZE;
        
        // Calculate destination on screen
        SDL_Rect dest;
        dest.x = (int)(tileX * FLOOR_TILE_SIZE - cameraX);
        dest.y = floorStartY + tileY * FLOOR_TILE_SIZE;
        dest.w = FLOOR_TILE_SIZE;
        dest.h = FLOOR_TILE_SIZE;
        
        SDL_BlitSurface(floorTexture, &source, screen, &dest);
      }
    }
  } else {
    // Fallback to solid color if texture not loaded
    Uint32 brown = SDL_MapRGB(screen->format, 0x8B, 0x4F, 0x2A);
    DrawFloor(screen, brown);
  }
}

void DrawPlayer(SDL_Surface *screen, SDL_Surface *spritemap, GameState *game) {
  int screenX = (int)(game->playerX - game->cameraX);
  int screenY = (int)game->playerY;

  // Calculate depth scale based on Y position
  // Y ranges from (SCREEN_HEIGHT - FLOOR_HEIGHT + 5) at top to SCREEN_HEIGHT at bottom
  double playAreaTop = SCREEN_HEIGHT - FLOOR_HEIGHT + 5;
  double playAreaBottom = SCREEN_HEIGHT;
  double normalizedY = (game->playerY - playAreaTop) / (playAreaBottom - playAreaTop);
  normalizedY = (normalizedY < 0) ? 0 : (normalizedY > 1) ? 1 : normalizedY;
  
  // Scale from DEPTH_SCALE_MAX at top to DEPTH_SCALE_MIN at bottom
  double depthScale = DEPTH_SCALE_MAX - (DEPTH_SCALE_MAX - DEPTH_SCALE_MIN) * normalizedY;
  
  SDL_Rect source, dest;
  source.w = SPRITE_SRC_WIDTH;
  source.h = SPRITE_SRC_HEIGHT;
  
  // Apply depth scaling to sprite dimensions
  dest.w = (int)(SPRITE_WIDTH * depthScale);
  dest.h = (int)(SPRITE_HEIGHT * depthScale);

  // sprites in first row:
  // standing(0) | crouch_defense(1) | light_defense_attack(2) |
  // hand_long_attack(3) | stomach_attack(4) | stomach_attack_prepare(5) |
  // jump_forward(6) | jump_landing(7)
  int spriteIndex = 0;
  int spriteRow = 0;

  if (game->state == STATE_CROUCH_DEFENSE) {
    spriteIndex = 1; // crouch_defense for defense
  } else if (game->state == STATE_HEAVY_ATTACK) {
    spriteIndex = 3; // hand_long_attack for heavy attack
  } else if (game->state == STATE_TRIPLE_LIGHT_ATTACK) {
    spriteIndex = 2; // light_defense_attack for triple light
  } else if (game->state == STATE_TRIPLE_HEAVY_ATTACK) {
    spriteIndex = 4; // stomach_attack for triple heavy
  } else if (game->state == STATE_COMBO_SEQUENCE) {
    spriteIndex = 5; // stomach_attack_prepare for combo
  } else if (game->state == STATE_DASH) {
    spriteRow = 2;   // Row 3 (index 2)
    spriteIndex = 1; // Column 2 (index 1) - dash sprite
  } else {
    spriteIndex = 0; // standing idle
  }

  source.x = spriteIndex * SPRITE_SRC_WIDTH;
  source.y = spriteRow * SPRITE_SRC_HEIGHT;
  dest.x = screenX;
  dest.y = screenY - dest.h;  // Use scaled height
  SDL_BlitScaled(spritemap, &source, screen, &dest);
}

void DrawDebugMenu(SDL_Surface *screen, SDL_Surface *charset, GameState *game) {
  char text[128];
  sprintf(text, "DEBUG MENU - Current Action: %s", game->currentActionName);
  DrawString(screen, 16, DEBUG_MENU_START + 4, text, charset);

  sprintf(text, "Input Buffer (%d):", game->bufferCount);
  DrawString(screen, 16, DEBUG_MENU_START + 18, text, charset);

  char bufferText[128] = "";
  for (int i = 0; i < game->bufferCount; i++) {
    const char *inputName = "?";
    switch (game->inputBuffer[i].type) {
    case INPUT_LIGHT:
      inputName = "R";
      break;
    case INPUT_HEAVY:
      inputName = "F";
      break;
    case INPUT_UP:
      inputName = "W";
      break;
    case INPUT_DOWN:
      inputName = "S";
      break;
    case INPUT_LEFT:
      inputName = "A";
      break;
    case INPUT_RIGHT:
      inputName = "D";
      break;
    }
    strcat(bufferText, inputName);
    if (i < game->bufferCount - 1)
      strcat(bufferText, "-");
  }
  sprintf(text, "[%s]", bufferText);
  DrawString(screen, 16, DEBUG_MENU_START + 32, text, charset);

  sprintf(text, "Time since last input: %.2lf s",
          game->worldTime - game->lastInputTime);
  DrawString(screen, 16, DEBUG_MENU_START + 46, text, charset);
}

void DrawHUD(SDL_Surface *screen, SDL_Surface *charset, GameState *game,
             double fps, Uint32 outlineColor, Uint32 fillColor) {
  char text[128];

  DrawRectangle(screen, 8, 8, SCREEN_WIDTH - 16, 50, outlineColor, fillColor);

  sprintf(text, "BEAT 'EM UP GAME");
  DrawString(screen, screen->w / 2 - strlen(text) * 12 / 2, 12, text, charset);

  sprintf(text, "time: %.1lf s | position: (%.0lf, %.0lf) | fps: %.0lf",
          game->worldTime, game->playerX, SCREEN_HEIGHT - game->playerY, fps);
  DrawString(screen, 16, 26, text, charset);

  sprintf(text, "exit [ESC] | new game [N] | move [WSAD] | crouch [R] | heavy "
                "attack [F] | debug menu [B]");
  DrawString(screen, 16, 40, text, charset);

  if (game->debugMenuEnabled) {
    DrawRectangle(screen, 8, DEBUG_MENU_START, SCREEN_WIDTH - 16, 60,
                  outlineColor, fillColor);
    DrawDebugMenu(screen, charset, game);
  }
}

void UpdatePlayerPosition(GameState *game, double delta) {
  double dx = 0, dy = 0;

  // don't allow movement during attacks
  if (game->state == STATE_IDLE) {
    if (game->moveLeft)
      dx -= PLAYER_SPEED * delta;
    if (game->moveRight)
      dx += PLAYER_SPEED * delta;
    if (game->moveUp)
      dy -= PLAYER_SPEED * delta;
    if (game->moveDown)
      dy += PLAYER_SPEED * delta;

    game->playerX += dx;
    game->playerY += dy;
  }

  // clamp player position within stage bounds
  if (game->playerX < 0)
    game->playerX = 0;
  if (game->playerX > STAGE_WIDTH - SPRITE_WIDTH)
    game->playerX = STAGE_WIDTH - SPRITE_WIDTH;
  if (game->playerY < SCREEN_HEIGHT - FLOOR_HEIGHT + 5)
    game->playerY = SCREEN_HEIGHT - FLOOR_HEIGHT + 5;
  if (game->playerY > SCREEN_HEIGHT)
    game->playerY = SCREEN_HEIGHT;
}

void UpdateAttackState(GameState *game, double delta) {
  if (game->state != STATE_IDLE) {
    game->attackTimer -= delta;

    if (game->attackTimer <= 0) {
      game->state = STATE_IDLE;
      strcpy(game->currentActionName, "idle");
    }
  }

  // Clean expired inputs from buffer
  CleanInputBuffer(game);
}

void StartLightAttack(GameState *game) {
  AddInput(game, INPUT_LIGHT);
  CheckCombos(game);

  // If no combo was triggered, do basic attack
  if (game->state == STATE_IDLE) {
    game->state = STATE_CROUCH_DEFENSE;
    game->attackTimer = CROUCH_DEFENSE_DURATION;
    strcpy(game->currentActionName, "crouch defense");
  }
}

void StartHeavyAttack(GameState *game) {
  AddInput(game, INPUT_HEAVY);
  CheckCombos(game);

  // If no combo was triggered, do basic attack
  if (game->state == STATE_IDLE) {
    game->state = STATE_HEAVY_ATTACK;
    game->attackTimer = HEAVY_ATTACK_DURATION;
    strcpy(game->currentActionName, "heavy attack");
  }
}

void UpdateCamera(GameState *game, double delta) {
  double targetCameraX = game->playerX - SCREEN_WIDTH / 2;

  if (targetCameraX < 0)
    targetCameraX = 0;
  if (targetCameraX > STAGE_WIDTH - SCREEN_WIDTH)
    targetCameraX = STAGE_WIDTH - SCREEN_WIDTH;

  double cameraDiff = targetCameraX - game->cameraX;
  game->cameraX += cameraDiff * CAMERA_SMOOTH_FACTOR * delta;
}

void CleanupSDL(SDL_Surface *charset, SDL_Surface *spritemap,
                SDL_Surface *cloudTexture, SDL_Surface *floorTexture,
                SDL_Surface *screen, SDL_Texture *texture,
                SDL_Renderer *renderer, SDL_Window *window) {
  if (charset)
    SDL_FreeSurface(charset);
  if (spritemap)
    SDL_FreeSurface(spritemap);
  if (cloudTexture)
    SDL_FreeSurface(cloudTexture);
  if (floorTexture)
    SDL_FreeSurface(floorTexture);
  SDL_FreeSurface(screen);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

int main(int argc, char **argv) {
  SDL_Event event;
  SDL_Surface *screen, *charset, *spritemap, *cloudTexture, *floorTexture;
  SDL_Texture *screenTexture;
  SDL_Window *window;
  SDL_Renderer *renderer;
  GameState game;

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    printf("SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }
  
  // Initialize random seed for floor generation
  srand(time(NULL));

  int rc = SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window,
                                       &renderer);
  if (rc != 0) {
    SDL_Quit();
    printf("SDL_CreateWindowAndRenderer error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_SetWindowTitle(window, "Beat 'em Up Game");

  screen = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0x00FF0000,
                                0x0000FF00, 0x000000FF, 0xFF000000);

  screenTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                                    SCREEN_HEIGHT);

  SDL_ShowCursor(SDL_DISABLE);

  charset = SDL_LoadBMP("./vcr_osd_mono.bmp");
  if (charset == NULL) {
    printf("SDL_LoadBMP(vcr_osd_mono.bmp) error: %s\n", SDL_GetError());
    CleanupSDL(NULL, NULL, NULL, NULL, screen, screenTexture, renderer, window);
    return 1;
  }
  SDL_SetColorKey(charset, true, 0x000000);

  spritemap = SDL_LoadBMP("./player_sprite.bmp");
  if (spritemap == NULL) {
    printf("SDL_LoadBMP(player_sprite.bmp) error: %s\n", SDL_GetError());
    CleanupSDL(charset, NULL, NULL, NULL, screen, screenTexture, renderer, window);
    return 1;
  }
  SDL_SetColorKey(spritemap, true, 0x000000);
  
  // Load cloud texture
  cloudTexture = SDL_LoadBMP("./texture_clouds.bmp");
  if (cloudTexture == NULL) {
    printf("Warning: SDL_LoadBMP(texture_clouds.bmp) error: %s\n", SDL_GetError());
    printf("Continuing without cloud texture...\n");
  }
  
  // Load floor texture
  floorTexture = SDL_LoadBMP("./texture_floor.bmp");
  if (floorTexture == NULL) {
    printf("Warning: SDL_LoadBMP(texture_floor.bmp) error: %s\n", SDL_GetError());
    printf("Continuing without floor texture...\n");
  }

  Uint32 black = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
  Uint32 brown = SDL_MapRGB(screen->format, 0x8B, 0x4F, 0x2A);

  InitGame(&game);

  int timeLast = SDL_GetTicks();
  int timeNow = 0;
  int frames = 0;
  int quit = 0;
  double fpsTimer = 0;
  double fps = 0;

  while (!quit) {
    timeNow = SDL_GetTicks();
    double delta = (timeNow - timeLast) * 0.001;
    timeLast = timeNow;

    game.worldTime += delta;
    UpdatePlayerPosition(&game, delta);
    UpdateAttackState(&game, delta);
    UpdateCamera(&game, delta);

    SDL_FillRect(screen, NULL, black);

    DrawStage(screen, game.cameraX, cloudTexture, floorTexture, &game);
    DrawPlayer(screen, spritemap, &game);

    fpsTimer += delta;
    if (fpsTimer > 0.5) {
      fps = frames * 2;
      frames = 0;
      fpsTimer -= 0.5;
    }

    DrawHUD(screen, charset, &game, fps, black, black);

    SDL_UpdateTexture(screenTexture, NULL, screen->pixels, screen->pitch);
    SDL_RenderCopy(renderer, screenTexture, NULL, NULL);
    SDL_RenderPresent(renderer);

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_KEYDOWN:
        if (event.key.keysym.sym == SDLK_ESCAPE)
          quit = 1;
        else if (event.key.keysym.sym == SDLK_n)
          InitGame(&game);
        else if (event.key.keysym.sym == SDLK_b)
          game.debugMenuEnabled = !game.debugMenuEnabled;
        else if (event.key.keysym.sym == SDLK_a) {
          if (!event.key.repeat) {                 // Ignore key repeats
            game.keyPressTime[1] = game.worldTime; // Track press time for left
          }
          game.moveLeft = 1;
        } else if (event.key.keysym.sym == SDLK_d) {
          if (!event.key.repeat) {
            game.keyPressTime[3] = game.worldTime; // Track press time for right
          }
          game.moveRight = 1;
        } else if (event.key.keysym.sym == SDLK_w) {
          if (!event.key.repeat) {
            game.keyPressTime[0] = game.worldTime; // Track press time for up
          }
          game.moveUp = 1;
        } else if (event.key.keysym.sym == SDLK_s) {
          if (!event.key.repeat) {
            game.keyPressTime[2] = game.worldTime; // Track press time for down
          }
          game.moveDown = 1;
        } else if (event.key.keysym.sym == SDLK_r)
          StartLightAttack(&game);
        else if (event.key.keysym.sym == SDLK_f)
          StartHeavyAttack(&game);
        break;
      case SDL_KEYUP:
        if (event.key.keysym.sym == SDLK_a) {
          game.moveLeft = 0;
          // Only add to buffer if it was a tap (quick press)
          if (game.worldTime - game.keyPressTime[1] < DASH_TAP_THRESHOLD) {
            AddInput(&game, INPUT_LEFT);
            CheckCombos(&game);
          }
        } else if (event.key.keysym.sym == SDLK_d) {
          game.moveRight = 0;
          if (game.worldTime - game.keyPressTime[3] < DASH_TAP_THRESHOLD) {
            AddInput(&game, INPUT_RIGHT);
            CheckCombos(&game);
          }
        } else if (event.key.keysym.sym == SDLK_w) {
          game.moveUp = 0;
          if (game.worldTime - game.keyPressTime[0] < DASH_TAP_THRESHOLD) {
            AddInput(&game, INPUT_UP);
            CheckCombos(&game);
          }
        } else if (event.key.keysym.sym == SDLK_s) {
          game.moveDown = 0;
          if (game.worldTime - game.keyPressTime[2] < DASH_TAP_THRESHOLD) {
            AddInput(&game, INPUT_DOWN);
            CheckCombos(&game);
          }
        }
        break;
      case SDL_QUIT:
        quit = 1;
        break;
      }
    }
    frames++;
  }

  CleanupSDL(charset, spritemap, cloudTexture, floorTexture, screen, screenTexture, renderer, window);
  return 0;
}
