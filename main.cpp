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

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define STAGE_WIDTH 2400
#define STAGE_HEIGHT 480
#define FLOOR_Y 350
#define FLOOR_HEIGHT 130
#define PLAYER_SPEED 150.0
#define CAMERA_SMOOTH_FACTOR 5.0

void DrawString(SDL_Surface *screen, int x, int y, const char *text,
                SDL_Surface *charset) {
  SDL_Rect source, dest;
  source.w = 8;
  source.h = 8;
  dest.w = 8;
  dest.h = 8;
  
  while (*text) {
    int character = *text & 255;
    source.x = (character % 16) * 8;
    source.y = (character / 16) * 8;
    dest.x = x;
    dest.y = y;
    SDL_BlitSurface(charset, &source, screen, &dest);
    x += 8;
    text++;
  }
}

void DrawPixel(SDL_Surface *surface, int x, int y, Uint32 color) {
  int bytesPerPixel = surface->format->BytesPerPixel;
  Uint8 *pixel = (Uint8 *)surface->pixels + y * surface->pitch + x * bytesPerPixel;
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
  bool gameActive;
  int moveLeft, moveRight, moveUp, moveDown;
};

void InitGame(GameState *game) {
  game->playerX = 100;
  game->playerY = FLOOR_Y;
  game->cameraX = 0;
  game->worldTime = 0;
  game->gameActive = true;
  game->moveLeft = 0;
  game->moveRight = 0;
  game->moveUp = 0;
  game->moveDown = 0;
}

void DrawSkyGradient(SDL_Surface *screen) {
  for (int y = 0; y < FLOOR_Y; y++) {
    int blue = 0x33 + (0x88 - 0x33) * y / FLOOR_Y;
    Uint32 skyColor = SDL_MapRGB(screen->format, 0x66, 0x99, blue);
    DrawLine(screen, 0, y, SCREEN_WIDTH, 1, 0, skyColor);
  }
}

void DrawFloor(SDL_Surface *screen, Uint32 brown) {
  for (int y = FLOOR_Y; y < SCREEN_HEIGHT; y++) {
    DrawLine(screen, 0, y, SCREEN_WIDTH, 1, 0, brown);
  }
}

void DrawFloorGrid(SDL_Surface *screen, double cameraX, Uint32 gray) {
  for (int x = 0; x < STAGE_WIDTH; x += 50) {
    int screenX = x - (int)cameraX;
    if (screenX >= -50 && screenX < SCREEN_WIDTH + 50) {
      DrawLine(screen, screenX, FLOOR_Y, FLOOR_HEIGHT, 0, 1, gray);
    }
  }
}

void DrawStageBoundaries(SDL_Surface *screen, double cameraX, Uint32 black) {
  int leftBound = 0 - (int)cameraX;
  int rightBound = STAGE_WIDTH - (int)cameraX;
  
  if (leftBound >= 0 && leftBound < SCREEN_WIDTH) {
    DrawLine(screen, leftBound, FLOOR_Y, FLOOR_HEIGHT, 0, 1, black);
    DrawLine(screen, leftBound + 1, FLOOR_Y, FLOOR_HEIGHT, 0, 1, black);
  }
  
  if (rightBound >= 0 && rightBound < SCREEN_WIDTH) {
    DrawLine(screen, rightBound, FLOOR_Y, FLOOR_HEIGHT, 0, 1, black);
    DrawLine(screen, rightBound - 1, FLOOR_Y, FLOOR_HEIGHT, 0, 1, black);
  }
}

void DrawStage(SDL_Surface *screen, double cameraX, Uint32 black, Uint32 gray,
               Uint32 brown) {
  DrawSkyGradient(screen);
  DrawFloor(screen, brown);
  DrawFloorGrid(screen, cameraX, gray);
  DrawStageBoundaries(screen, cameraX, black);
}

void DrawPlayer(SDL_Surface *screen, double playerX, double playerY,
                double cameraX, Uint32 black, Uint32 red, Uint32 green) {
  int screenX = (int)(playerX - cameraX);
  int screenY = (int)playerY;

  DrawRectangle(screen, screenX - 10, screenY - 30, 20, 30, black, red);
  DrawRectangle(screen, screenX - 8, screenY - 45, 16, 15, black, green);
  DrawRectangle(screen, screenX - 15, screenY - 25, 5, 15, black, red);
  DrawRectangle(screen, screenX + 10, screenY - 25, 5, 15, black, red);
  DrawRectangle(screen, screenX - 8, screenY, 6, 10, black, red);
  DrawRectangle(screen, screenX + 2, screenY, 6, 10, black, red);
}

void DrawHUD(SDL_Surface *screen, SDL_Surface *charset, GameState *game,
             double fps, Uint32 red, Uint32 blue) {
  char text[128];

  DrawRectangle(screen, 4, 4, SCREEN_WIDTH - 8, 56, red, blue);

  sprintf(text, "Beat 'em Up Game");
  DrawString(screen, screen->w / 2 - strlen(text) * 8 / 2, 10, text, charset);

  sprintf(text, "Time: %.1lf s | Position: (%.0lf, %.0lf) | FPS: %.0lf",
          game->worldTime, game->playerX, game->playerY, fps);
  DrawString(screen, 12, 26, text, charset);

  sprintf(text, "ESC-exit | N-new game | WSAD-move");
  DrawString(screen, 12, 42, text, charset);
}

void UpdatePlayerPosition(GameState *game, double delta) {
  double dx = 0, dy = 0;
  
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

  if (game->playerX < 20)
    game->playerX = 20;
  if (game->playerX > STAGE_WIDTH - 20)
    game->playerX = STAGE_WIDTH - 20;
  if (game->playerY < FLOOR_Y)
    game->playerY = FLOOR_Y;
  if (game->playerY > SCREEN_HEIGHT - 20)
    game->playerY = SCREEN_HEIGHT - 20;
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

void CleanupSDL(SDL_Surface *charset, SDL_Surface *screen, SDL_Texture *texture,
                SDL_Renderer *renderer, SDL_Window *window) {
  if (charset)
    SDL_FreeSurface(charset);
  SDL_FreeSurface(screen);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

#ifdef __cplusplus
extern "C"
#endif
    int main(int argc, char **argv) {
  SDL_Event event;
  SDL_Surface *screen, *charset;
  SDL_Texture *screenTexture;
  SDL_Window *window;
  SDL_Renderer *renderer;
  GameState game;
  
  int timeNow, timeLast, quit, frames;
  double delta, fpsTimer, fps;

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

  charset = SDL_LoadBMP("./cs8x8.bmp");
  if (charset == NULL) {
    printf("SDL_LoadBMP(cs8x8.bmp) error: %s\n", SDL_GetError());
    CleanupSDL(NULL, screen, screenTexture, renderer, window);
    return 1;
  }
  SDL_SetColorKey(charset, true, 0x000000);

  Uint32 black = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
  Uint32 green = SDL_MapRGB(screen->format, 0x00, 0xFF, 0x00);
  Uint32 red = SDL_MapRGB(screen->format, 0xFF, 0x00, 0x00);
  Uint32 blue = SDL_MapRGB(screen->format, 0x11, 0x11, 0xCC);
  Uint32 gray = SDL_MapRGB(screen->format, 0x80, 0x80, 0x80);
  Uint32 brown = SDL_MapRGB(screen->format, 0x8B, 0x4F, 0x2A);

  InitGame(&game);

  timeLast = SDL_GetTicks();
  frames = 0;
  fpsTimer = 0;
  fps = 0;
  quit = 0;

  while (!quit) {
    timeNow = SDL_GetTicks();
    delta = (timeNow - timeLast) * 0.001;
    timeLast = timeNow;

    if (game.gameActive) {
      game.worldTime += delta;
      UpdatePlayerPosition(&game, delta);
      UpdateCamera(&game, delta);
    }

    SDL_FillRect(screen, NULL, black);

    if (game.gameActive) {
      DrawStage(screen, game.cameraX, black, gray, brown);
      DrawPlayer(screen, game.playerX, game.playerY, game.cameraX, black, red,
                 green);
    } else {
      char text[128];
      DrawRectangle(screen, SCREEN_WIDTH / 2 - 150, SCREEN_HEIGHT / 2 - 50, 300,
                    100, red, blue);
      sprintf(text, "Press 'N' to start");
      DrawString(screen, SCREEN_WIDTH / 2 - strlen(text) * 8 / 2,
                 SCREEN_HEIGHT / 2 - 10, text, charset);
      sprintf(text, "ESC to exit");
      DrawString(screen, SCREEN_WIDTH / 2 - strlen(text) * 8 / 2,
                 SCREEN_HEIGHT / 2 + 10, text, charset);
    }

    fpsTimer += delta;
    if (fpsTimer > 0.5) {
      fps = frames * 2;
      frames = 0;
      fpsTimer -= 0.5;
    }

    DrawHUD(screen, charset, &game, fps, red, blue);

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

  CleanupSDL(charset, screen, screenTexture, renderer, window);
  return 0;
}
