#include <stdio.h>
#include <string.h>

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

#define FONT_SIZE 10
#define FONT_SRC_WIDTH 20
#define FONT_SRC_HEIGHT 26

#define SPRITE_SRC_WIDTH 46
#define SPRITE_SRC_HEIGHT 50
#define SPRITE_WIDTH 64
#define SPRITE_HEIGHT ((int)(SPRITE_SRC_HEIGHT * ((double)SPRITE_WIDTH / SPRITE_SRC_WIDTH)))

#define LIGHT_ATTACK_FRAMES 15
#define HEAVY_ATTACK_FRAMES 30
#define LIGHT_ATTACK_DURATION 0.3
#define HEAVY_ATTACK_DURATION 0.6

enum PlayerState {
  STATE_IDLE = 0,
  STATE_LIGHT_ATTACK = 1,
  STATE_HEAVY_ATTACK = 2
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

struct GameState {
  double playerX, playerY;
  double cameraX;
  double worldTime;
  int moveLeft, moveRight, moveUp, moveDown;
  PlayerState state;
  double attackTimer;
  int animationFrame;
};

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
  game->animationFrame = 0;
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

void DrawStage(SDL_Surface *screen, double cameraX, Uint32 black,
               Uint32 brown) {
  DrawSkyGradient(screen);
  DrawFloor(screen, brown);
}

void DrawPlayer(SDL_Surface *screen, SDL_Surface *spritemap, GameState *game) {
  int screenX = (int)(game->playerX - game->cameraX);
  int screenY = (int)game->playerY;

  SDL_Rect source, dest;
  source.w = SPRITE_SRC_WIDTH;
  source.h = SPRITE_SRC_HEIGHT;
  dest.w = SPRITE_WIDTH;
  dest.h = SPRITE_HEIGHT;

  // sprites in first row:
  // standing(0) | attack_crouch(1) | light_defense_attack(2) |
  // hand_long_attack(3) | stomach_attack(4) | stomach_attack_prepare(5) |
  // jump_forward(6) | jump_landing(7)
  int spriteIndex = 0;
  if (game->state == STATE_LIGHT_ATTACK) {
    spriteIndex = 2; // light_defense_attack for light attack
  } else if (game->state == STATE_HEAVY_ATTACK) {
    spriteIndex = 3; // hand_long_attack for heavy attack
  } else {
    spriteIndex = 0; // standing idle
  }

  source.x = spriteIndex * SPRITE_SRC_WIDTH;
  source.y = 0;
  dest.x = screenX;
  dest.y = screenY - SPRITE_HEIGHT;
  SDL_BlitScaled(spritemap, &source, screen, &dest);
}

void DrawHUD(SDL_Surface *screen, SDL_Surface *charset, GameState *game,
             double fps, Uint32 outlineColor, Uint32 fillColor) {
  char text[128];

  DrawRectangle(screen, 8, 8, SCREEN_WIDTH - 16, 62, outlineColor, fillColor);

  sprintf(text, "BEAT 'EM UP GAME");
  DrawString(screen, screen->w / 2 - strlen(text) * 12 / 2, 12, text, charset);

  sprintf(text, "time: %.1lf s | position: (%.0lf, %.0lf) | fps: %.0lf",
          game->worldTime, game->playerX, SCREEN_HEIGHT - game->playerY, fps);
  DrawString(screen, 16, 26, text, charset);

  sprintf(text, "move [WSAD] | light attack [R] | heavy attack [F]");
  DrawString(screen, 16, 40, text, charset);

  sprintf(text, "exit [ESC] | new game [N]");
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
    game->attackTimer += delta;

    double attackDuration = 0;
    int totalFrames = 0;

    if (game->state == STATE_LIGHT_ATTACK) {
      attackDuration = LIGHT_ATTACK_DURATION;
      totalFrames = LIGHT_ATTACK_FRAMES;
    } else if (game->state == STATE_HEAVY_ATTACK) {
      attackDuration = HEAVY_ATTACK_DURATION;
      totalFrames = HEAVY_ATTACK_FRAMES;
    }

    game->animationFrame =
        (int)(game->attackTimer / attackDuration * totalFrames);

    if (game->attackTimer >= attackDuration) {
      game->state = STATE_IDLE;
      game->attackTimer = 0;
      game->animationFrame = 0;
    }
  }
}

void StartLightAttack(GameState *game) {
  if (game->state == STATE_IDLE) {
    game->state = STATE_LIGHT_ATTACK;
    game->attackTimer = 0;
    game->animationFrame = 0;
  }
}

void StartHeavyAttack(GameState *game) {
  if (game->state == STATE_IDLE) {
    game->state = STATE_HEAVY_ATTACK;
    game->attackTimer = 0;
    game->animationFrame = 0;
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
                SDL_Surface *screen, SDL_Texture *texture,
                SDL_Renderer *renderer, SDL_Window *window) {
  if (charset)
    SDL_FreeSurface(charset);
  if (spritemap)
    SDL_FreeSurface(spritemap);
  SDL_FreeSurface(screen);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

int main(int argc, char **argv) {
  SDL_Event event;
  SDL_Surface *screen, *charset, *spritemap;
  SDL_Texture *screenTexture;
  SDL_Window *window;
  SDL_Renderer *renderer;
  GameState game;

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    printf("SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

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
    CleanupSDL(NULL, NULL, screen, screenTexture, renderer, window);
    return 1;
  }
  SDL_SetColorKey(charset, true, 0x000000);

  spritemap = SDL_LoadBMP("./player_sprite.bmp");
  if (spritemap == NULL) {
    printf("SDL_LoadBMP(player_sprite.bmp) error: %s\n", SDL_GetError());
    CleanupSDL(charset, NULL, screen, screenTexture, renderer, window);
    return 1;
  }
  SDL_SetColorKey(spritemap, true, 0x000000);

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

    DrawStage(screen, game.cameraX, black, brown);
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
        else if (event.key.keysym.sym == SDLK_a)
          game.moveLeft = 1;
        else if (event.key.keysym.sym == SDLK_d)
          game.moveRight = 1;
        else if (event.key.keysym.sym == SDLK_w)
          game.moveUp = 1;
        else if (event.key.keysym.sym == SDLK_s)
          game.moveDown = 1;
        else if (event.key.keysym.sym == SDLK_r)
          StartLightAttack(&game);
        else if (event.key.keysym.sym == SDLK_f)
          StartHeavyAttack(&game);
        break;
      case SDL_KEYUP:
        if (event.key.keysym.sym == SDLK_a)
          game.moveLeft = 0;
        else if (event.key.keysym.sym == SDLK_d)
          game.moveRight = 0;
        else if (event.key.keysym.sym == SDLK_w)
          game.moveUp = 0;
        else if (event.key.keysym.sym == SDLK_s)
          game.moveDown = 0;
        break;
      case SDL_QUIT:
        quit = 1;
        break;
      }
    }
    frames++;
  }

  CleanupSDL(charset, spritemap, screen, screenTexture, renderer, window);
  return 0;
}
