#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define FONT_SIZE 10
#define FONT_SRC_WIDTH 20
#define FONT_SRC_HEIGHT 26

#define SPRITE_SRC_WIDTH 46
#define SPRITE_SRC_HEIGHT 50
#define SPRITE_WIDTH 64
#define SPRITE_HEIGHT                                                          \
  ((int)(SPRITE_SRC_HEIGHT * ((double)SPRITE_WIDTH / SPRITE_SRC_WIDTH)))

// input buffer parameters
#define INPUT_BUFFER_SIZE 5
#define INPUT_TIMEOUT 0.8       // seconds before input expires
#define INPUT_TAP_THRESHOLD 0.4 // max time for a key press to count as a tap

// attack durations
#define DASH_DURATION 0.3
#define CROUCH_DEFENSE_DURATION 0.3
#define HEAVY_ATTACK_DURATION 0.6
#define TRIPLE_ATTACK_DURATION 1.5
#define COMBO_SEQUENCE_DURATION 2.0

// depth scaling parameters
#define DEPTH_SCALE_MIN 0.8
#define DEPTH_SCALE_MAX 1.2

// utility macros
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

const int SKY_HEIGHT = SCREEN_HEIGHT - FLOOR_HEIGHT;

enum InputType {
  INPUT_NONE = 0,
  INPUT_UP = 1,
  INPUT_DOWN = 2,
  INPUT_LEFT = 3,
  INPUT_RIGHT = 4,
  INPUT_LIGHT_ATTACK = 5,
  INPUT_HEAVY_ATTACK = 6
};
const char *InputTypeNames[] = {"?",     "UP",    "DOWN", "LEFT",
                                "RIGHT", "LIGHT", "HEAVY"};

enum PlayerState {
  STATE_IDLE = 0,
  STATE_CROUCH_DEFENSE = 1,
  STATE_HEAVY_ATTACK = 2,
  STATE_TRIPLE_LIGHT_ATTACK = 3,
  STATE_TRIPLE_HEAVY_ATTACK = 4,
  STATE_COMBO_SEQUENCE = 5,
  STATE_DASH = 6
};
const char *PlayerStateNames[] = {
    "IDLE",         "CROUCH_DEFENSE", "HEAVY_ATTACK", "TRIPLE_LIGHT",
    "TRIPLE_HEAVY", "COMBO_SEQUENCE", "DASH"};

struct BufferInputEntry {
  InputType type;
  double timestamp;
};

struct GameState {
  double playerX, playerY;
  double cameraX;
  double worldTime;
  int moveLeft, moveRight, moveUp, moveDown;
  PlayerState state;
  double attackTimer;

  BufferInputEntry inputBuffer[INPUT_BUFFER_SIZE];
  int bufferCount;
  double lastInputTime;
  double keyPressTime[4];

  int debugMenuEnabled;
};

struct SDLResources {
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Surface *screen;
  SDL_Texture *screenTexture;
  SDL_Surface *charset;
  SDL_Surface *spritemap;
  SDL_Surface *cloudTexture;
  SDL_Surface *floorTexture;
};

struct ComboDefinition {
  PlayerState resultState;
  InputType sequence[3]; // max 3 inputs in a combo
  double duration;
};

ComboDefinition combos[] = {
    {
        STATE_TRIPLE_LIGHT_ATTACK,
        {INPUT_LIGHT_ATTACK, INPUT_LIGHT_ATTACK, INPUT_LIGHT_ATTACK},
        TRIPLE_ATTACK_DURATION,
    },
    {
        STATE_TRIPLE_HEAVY_ATTACK,
        {INPUT_HEAVY_ATTACK, INPUT_HEAVY_ATTACK, INPUT_HEAVY_ATTACK},
        TRIPLE_ATTACK_DURATION,
    },
    {
        STATE_COMBO_SEQUENCE,
        {INPUT_LIGHT_ATTACK, INPUT_HEAVY_ATTACK, INPUT_LIGHT_ATTACK},
        COMBO_SEQUENCE_DURATION,
    },
    {STATE_DASH, {INPUT_LEFT, INPUT_LEFT, INPUT_NONE}, DASH_DURATION},
    {STATE_DASH, {INPUT_RIGHT, INPUT_RIGHT, INPUT_NONE}, DASH_DURATION},
    {STATE_DASH, {INPUT_UP, INPUT_UP, INPUT_NONE}, DASH_DURATION},
    {STATE_DASH, {INPUT_DOWN, INPUT_DOWN, INPUT_NONE}, DASH_DURATION}};

const int comboCount = sizeof(combos) / sizeof(ComboDefinition);

void DrawString(SDL_Surface *screen, int x, int y, const char *text,
                SDL_Surface *charset) {
  SDL_Rect source, dest;
  source.w = FONT_SRC_WIDTH;
  source.h = FONT_SRC_HEIGHT;
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

void DrawBox(SDL_Surface *screen, int x, int y, int width, int height,
             Uint32 fillColor) {
  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      DrawPixel(screen, x + j, y + i, fillColor);
    }
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

  for (int i = 0; i < 4; i++) {
    game->keyPressTime[i] = 0;
  }
}

void AddInput(GameState *game, InputType input) {
  if (game->bufferCount < INPUT_BUFFER_SIZE) {
    game->bufferCount++;
  }

  for (int i = game->bufferCount - 1; i > 0; i--) {
    game->inputBuffer[i] = game->inputBuffer[i - 1];
  }

  game->inputBuffer[0].type = input;
  game->inputBuffer[0].timestamp = game->worldTime;
  game->lastInputTime = game->worldTime;
}

void CleanInputBuffer(GameState *game) {
  int cutoff = 0;
  for (cutoff = 0; cutoff < game->bufferCount; cutoff++) {
    if (game->worldTime - game->inputBuffer[cutoff].timestamp > INPUT_TIMEOUT) {
      break;
    }
  }

  game->bufferCount = cutoff;
}

bool MatchesCombo(GameState *game, ComboDefinition *combo) {
  for (int i = 0; i <= 3; i++) {
    InputType comboInput = combo->sequence[i];
    if (comboInput == INPUT_NONE)
      break;

    // check if input matches or if buffer is too short
    if (game->inputBuffer[i].type != comboInput || i >= game->bufferCount) {
      return false;
    }
  }
  return true;
}

void CheckCombos(GameState *game) {
  // allow interrupt of basic attacks with combos, but not combo attacks
  bool canInterrupt =
      (game->state == STATE_IDLE || game->state == STATE_CROUCH_DEFENSE ||
       game->state == STATE_HEAVY_ATTACK);
  if (!canInterrupt)
    return;

  for (int i = 0; i < comboCount; i++) {
    if (!MatchesCombo(game, &combos[i]))
      continue;

    game->state = combos[i].resultState;
    game->attackTimer = combos[i].duration;
    game->bufferCount = 0;

    return;
  }
}

void DrawStage(SDL_Surface *screen, double cameraX, SDL_Surface *cloudTexture,
               SDL_Surface *floorTexture, GameState *game) {
  for (int x = 0; x < SCREEN_WIDTH; x += CLOUD_TEXTURE_WIDTH) {
    for (int y = 0; y < SKY_HEIGHT; y += CLOUD_TEXTURE_HEIGHT) {
      SDL_Rect dest;
      dest.x = x;
      dest.y = y;
      dest.w = CLOUD_TEXTURE_WIDTH;
      dest.h = CLOUD_TEXTURE_HEIGHT;
      SDL_BlitSurface(cloudTexture, NULL, screen, &dest);
    }
  }

  int startTileX = MAX(0, (int)(cameraX / FLOOR_TILE_SIZE));
  int endTileX = MIN(startTileX + (SCREEN_WIDTH / FLOOR_TILE_SIZE) + 2,
                     FLOOR_TILES_HORIZONTAL);

  SDL_Rect sourceTile;
  sourceTile.x = 0;
  sourceTile.y = 0;
  sourceTile.w = FLOOR_TILE_SIZE;
  sourceTile.h = FLOOR_TILE_SIZE;

  for (int tileX = startTileX; tileX < endTileX; tileX++) {
    for (int tileY = 0; tileY < FLOOR_TILES_VERTICAL; tileY++) {
      SDL_Rect dest;
      dest.x = (int)(tileX * FLOOR_TILE_SIZE - cameraX);
      dest.y = SKY_HEIGHT + tileY * FLOOR_TILE_SIZE;
      dest.w = FLOOR_TILE_SIZE;
      dest.h = FLOOR_TILE_SIZE;

      SDL_BlitSurface(floorTexture, &sourceTile, screen, &dest);
    }
  }
}

void DrawPlayer(SDL_Surface *screen, SDL_Surface *spritemap, GameState *game) {
  int screenX = (int)(game->playerX - game->cameraX);
  int screenY = (int)game->playerY;

  double playerVerticalProgress =
      MAX(0, MIN(1.0, (double)(SCREEN_HEIGHT - screenY) / FLOOR_HEIGHT));

  double depthScale = DEPTH_SCALE_MAX - (DEPTH_SCALE_MAX - DEPTH_SCALE_MIN) *
                                            playerVerticalProgress;

  SDL_Rect source, dest;
  source.w = SPRITE_SRC_WIDTH;
  source.h = SPRITE_SRC_HEIGHT;

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
    spriteRow = 2;   // row 3 (index 2)
    spriteIndex = 1; // column 2 (index 1)
  }

  source.x = spriteIndex * SPRITE_SRC_WIDTH;
  source.y = spriteRow * SPRITE_SRC_HEIGHT;
  dest.x = screenX;
  dest.y = screenY - dest.h; // use scaled height to position correctly
  SDL_BlitScaled(spritemap, &source, screen, &dest);
}

void DrawDebugMenu(SDL_Surface *screen, SDL_Surface *charset, GameState *game,
                   Uint32 fillColor) {
  const int DEBUG_MENU_START = 80;

  DrawBox(screen, 8, DEBUG_MENU_START, SCREEN_WIDTH - 16, 48, fillColor);

  char text[128];
  snprintf(text, sizeof(text), "DEBUG MENU - Current Action: %s",
           PlayerStateNames[(int)game->state]);
  DrawString(screen, 16, DEBUG_MENU_START + 4, text, charset);

  snprintf(text, sizeof(text), "Input Buffer (%d):", game->bufferCount);
  DrawString(screen, 16, DEBUG_MENU_START + 18, text, charset);

  char bufferText[128] = "";
  int bufferPos = 0;
  for (int i = 0; i < game->bufferCount; i++) {
    InputType inputType = game->inputBuffer[i].type;
    bufferPos += snprintf(
        bufferText + bufferPos, sizeof(bufferText) - bufferPos, "%s%s",
        InputTypeNames[(int)inputType], (i < game->bufferCount - 1) ? "-" : "");
  }

  snprintf(text, sizeof(text), "[%s]", bufferText);
  DrawString(screen, 16, DEBUG_MENU_START + 32, text, charset);
}

void DrawHUD(SDL_Surface *screen, SDL_Surface *charset, GameState *game,
             double fps, Uint32 fillColor) {
  DrawBox(screen, 8, 8, SCREEN_WIDTH - 16, 66, fillColor);

  char text[128];
  snprintf(text, sizeof(text), "BEAT 'EM UP GAME");
  DrawString(screen, screen->w / 2 - strlen(text) * 12 / 2, 12, text, charset);

  snprintf(text, sizeof(text),
           "time: %.1lf s | position: (%.0lf, %.0lf) | fps: %.0lf",
           game->worldTime, game->playerX, SCREEN_HEIGHT - game->playerY, fps);
  DrawString(screen, 16, 26, text, charset);

  snprintf(text, sizeof(text),
           "implemented requirements: mandatory 1-4, optional A-B");
  DrawString(screen, 16, 40, text, charset);

  snprintf(text, sizeof(text),
           "exit [ESC] | new game [N] | move [WSAD] | crouch [R] | heavy "
           "attack [F] | debug menu [B]");
  DrawString(screen, 16, 54, text, charset);
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
    }
  }

  CleanInputBuffer(game);
}

void StartLightAttack(GameState *game) {
  AddInput(game, INPUT_LIGHT_ATTACK);
  CheckCombos(game);

  // if no combo was triggered, do basic attack
  if (game->state == STATE_IDLE) {
    game->state = STATE_CROUCH_DEFENSE;
    game->attackTimer = CROUCH_DEFENSE_DURATION;
  }
}

void StartHeavyAttack(GameState *game) {
  AddInput(game, INPUT_HEAVY_ATTACK);
  CheckCombos(game);

  // if no combo was triggered, do basic attack
  if (game->state == STATE_IDLE) {
    game->state = STATE_HEAVY_ATTACK;
    game->attackTimer = HEAVY_ATTACK_DURATION;
  }
}

void HandleKeyMovement(GameState *game, SDL_Keycode key, int isKeyDown,
                       bool repeat) {
  int *direction = NULL;
  InputType inputType = INPUT_NONE;

  switch (key) {
  case SDLK_a:
    direction = &game->moveLeft;
    inputType = INPUT_LEFT;
    break;

  case SDLK_d:
    direction = &game->moveRight;
    inputType = INPUT_RIGHT;
    break;
  case SDLK_w:
    direction = &game->moveUp;
    inputType = INPUT_UP;
    break;
  case SDLK_s:
    direction = &game->moveDown;
    inputType = INPUT_DOWN;
    break;
  default:
    return;
  }

  int keyIndex = (int)inputType - 1;

  if (isKeyDown) {
    *direction = 1;

    if (!repeat) {
      game->keyPressTime[keyIndex] = game->worldTime;
    }
  } else {
    *direction = 0;

    if (game->worldTime - game->keyPressTime[keyIndex] < INPUT_TAP_THRESHOLD) {
      AddInput(game, inputType);
      CheckCombos(game);
    }
  }
}

void HandleEvents(GameState *game, int *quit) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_KEYDOWN:
      if (event.key.keysym.sym == SDLK_ESCAPE)
        *quit = 1;
      else if (event.key.keysym.sym == SDLK_n)
        InitGame(game);
      else if (event.key.keysym.sym == SDLK_b)
        game->debugMenuEnabled = !game->debugMenuEnabled;
      else if (event.key.keysym.sym == SDLK_r)
        StartLightAttack(game);
      else if (event.key.keysym.sym == SDLK_f)
        StartHeavyAttack(game);
      else
        HandleKeyMovement(game, event.key.keysym.sym, true, event.key.repeat);
      break;
    case SDL_KEYUP:
      HandleKeyMovement(game, event.key.keysym.sym, false, NULL);
      break;
    case SDL_QUIT:
      *quit = 1;
      break;
    }
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

void RenderFrame(SDLResources *res, GameState *game, double fps) {
  Uint32 black = SDL_MapRGB(res->screen->format, 0x00, 0x00, 0x00);

  SDL_FillRect(res->screen, NULL, black);

  DrawStage(res->screen, game->cameraX, res->cloudTexture, res->floorTexture,
            game);
  DrawPlayer(res->screen, res->spritemap, game);
  DrawHUD(res->screen, res->charset, game, fps, black);

  if (game->debugMenuEnabled) {
    DrawDebugMenu(res->screen, res->charset, game, black);
  }

  SDL_UpdateTexture(res->screenTexture, NULL, res->screen->pixels,
                    res->screen->pitch);
  SDL_RenderCopy(res->renderer, res->screenTexture, NULL, NULL);
  SDL_RenderPresent(res->renderer);
}

void CleanupSDL(SDLResources *res) {
  if (res->charset)
    SDL_FreeSurface(res->charset);
  if (res->spritemap)
    SDL_FreeSurface(res->spritemap);
  if (res->cloudTexture)
    SDL_FreeSurface(res->cloudTexture);
  if (res->floorTexture)
    SDL_FreeSurface(res->floorTexture);
  if (res->screen)
    SDL_FreeSurface(res->screen);
  if (res->screenTexture)
    SDL_DestroyTexture(res->screenTexture);
  if (res->renderer)
    SDL_DestroyRenderer(res->renderer);
  if (res->window)
    SDL_DestroyWindow(res->window);
  SDL_Quit();
}

bool InitializeSDL(SDLResources *res) {
  memset(res, 0, sizeof(SDLResources));

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    printf("SDL_Init error: %s\n", SDL_GetError());
    return false;
  }

  int rc = SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0,
                                       &res->window, &res->renderer);
  if (rc != 0) {
    printf("SDL_CreateWindowAndRenderer error: %s\n", SDL_GetError());
    SDL_Quit();
    return false;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  SDL_RenderSetLogicalSize(res->renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
  SDL_SetRenderDrawColor(res->renderer, 0, 0, 0, 255);
  SDL_SetWindowTitle(res->window, "Beat 'em Up Game");

  res->screen =
      SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0x00FF0000,
                           0x0000FF00, 0x000000FF, 0xFF000000);

  res->screenTexture = SDL_CreateTexture(
      res->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      SCREEN_WIDTH, SCREEN_HEIGHT);

  SDL_ShowCursor(SDL_DISABLE);
  return true;
}

bool LoadResources(SDLResources *res) {
  res->charset = SDL_LoadBMP("./vcr_osd_mono.bmp");
  if (res->charset == NULL) {
    printf("SDL_LoadBMP(vcr_osd_mono.bmp) error: %s\n", SDL_GetError());
    return false;
  }

  res->spritemap = SDL_LoadBMP("./player_sprite.bmp");
  if (res->spritemap == NULL) {
    printf("SDL_LoadBMP(player_sprite.bmp) error: %s\n", SDL_GetError());
    return false;
  }

  res->cloudTexture = SDL_LoadBMP("./texture_clouds.bmp");
  if (res->cloudTexture == NULL) {
    printf("SDL_LoadBMP(texture_clouds.bmp) error: %s\n", SDL_GetError());
    return false;
  }

  res->floorTexture = SDL_LoadBMP("./texture_floor.bmp");
  if (res->floorTexture == NULL) {
    printf("SDL_LoadBMP(texture_floor.bmp) error: %s\n", SDL_GetError());
    return false;
  }

  return true;
}

int main(int argc, char **argv) {
  SDLResources res;
  GameState game;

  srand(time(NULL));

  if (!InitializeSDL(&res)) {
    return 1;
  }

  if (!LoadResources(&res)) {
    CleanupSDL(&res);
    return 1;
  }

  InitGame(&game);

  int quit = 0;

  int timeLast = SDL_GetTicks();
  int frames = 0;
  double fpsTimer = 0;
  double fps = 0;

  while (!quit) {
    int timeNow = SDL_GetTicks();
    double delta = (timeNow - timeLast) * 0.001;
    timeLast = timeNow;

    game.worldTime += delta;
    UpdatePlayerPosition(&game, delta);
    UpdateAttackState(&game, delta);
    UpdateCamera(&game, delta);

    fpsTimer += delta;
    if (fpsTimer > 0.5) {
      fps = frames * 2;
      frames = 0;
      fpsTimer -= 0.5;
    }

    RenderFrame(&res, &game, fps);
    HandleEvents(&game, &quit);

    frames++;
  }

  CleanupSDL(&res);
  return 0;
}
