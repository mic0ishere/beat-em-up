#define _USE_MATH_DEFINES
#include <math.h>
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

// Game constants
#define STAGE_WIDTH 2400  // Stage is wider than screen for scrolling
#define STAGE_HEIGHT 480
#define FLOOR_Y 350       // Floor y-coordinate
#define FLOOR_HEIGHT 130  // Height of the floor area
#define PLAYER_SPEED 150.0 // Pixels per second
#define CAMERA_GRACE 200   // Grace space before camera starts following

// narysowanie napisu txt na powierzchni screen, zaczynaj�c od punktu (x, y)
// charset to bitmapa 128x128 zawieraj�ca znaki
// draw a text txt on surface screen, starting from the point (x, y)
// charset is a 128x128 bitmap containing character images
void DrawString(SDL_Surface *screen, int x, int y, const char *text,
                SDL_Surface *charset) {
  int px, py, c;
  SDL_Rect s, d;
  s.w = 8;
  s.h = 8;
  d.w = 8;
  d.h = 8;
  while (*text) {
    c = *text & 255;
    px = (c % 16) * 8;
    py = (c / 16) * 8;
    s.x = px;
    s.y = py;
    d.x = x;
    d.y = y;
    SDL_BlitSurface(charset, &s, screen, &d);
    x += 8;
    text++;
  };
};

// narysowanie na ekranie screen powierzchni sprite w punkcie (x, y)
// (x, y) to punkt �rodka obrazka sprite na ekranie
// draw a surface sprite on a surface screen in point (x, y)
// (x, y) is the center of sprite on screen
void DrawSurface(SDL_Surface *screen, SDL_Surface *sprite, int x, int y) {
  SDL_Rect dest;
  dest.x = x - sprite->w / 2;
  dest.y = y - sprite->h / 2;
  dest.w = sprite->w;
  dest.h = sprite->h;
  SDL_BlitSurface(sprite, NULL, screen, &dest);
};

// rysowanie pojedynczego pixela
// draw a single pixel
void DrawPixel(SDL_Surface *surface, int x, int y, Uint32 color) {
  int bpp = surface->format->BytesPerPixel;
  Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;
  *(Uint32 *)p = color;
};

// rysowanie linii o d�ugo�ci l w pionie (gdy dx = 0, dy = 1)
// b�d� poziomie (gdy dx = 1, dy = 0)
// draw a vertical (when dx = 0, dy = 1) or horizontal (when dx = 1, dy = 0)
// line
void DrawLine(SDL_Surface *screen, int x, int y, int l, int dx, int dy,
              Uint32 color) {
  for (int i = 0; i < l; i++) {
    DrawPixel(screen, x, y, color);
    x += dx;
    y += dy;
  };
};

// rysowanie prostok�ta o d�ugo�ci bok�w l i k
// draw a rectangle of size l by k
void DrawRectangle(SDL_Surface *screen, int x, int y, int l, int k,
                   Uint32 outlineColor, Uint32 fillColor) {
  int i;
  DrawLine(screen, x, y, k, 0, 1, outlineColor);
  DrawLine(screen, x + l - 1, y, k, 0, 1, outlineColor);
  DrawLine(screen, x, y, l, 1, 0, outlineColor);
  DrawLine(screen, x, y + k - 1, l, 1, 0, outlineColor);
  for (i = y + 1; i < y + k - 1; i++)
    DrawLine(screen, x + 1, i, l - 2, 1, 0, fillColor);
};

// Game state structure
struct GameState {
  double playerX, playerY;  // Player position
  double cameraX;           // Camera X position
  double worldTime;         // Elapsed game time
  bool gameActive;          // Is game in progress
  int moveLeft, moveRight, moveUp, moveDown; // Movement flags
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

// Draw the game stage (floor and background)
void DrawStage(SDL_Surface *screen, double cameraX, int czarny, int zielony,
               int szary, int brazowy) {
  // Sky/background
  for (int i = 0; i < FLOOR_Y; i++) {
    int blue = 0x33 + (0x88 - 0x33) * i / FLOOR_Y;
    int skyColor = SDL_MapRGB(screen->format, 0x66, 0x99, blue);
    DrawLine(screen, 0, i, SCREEN_WIDTH, 1, 0, skyColor);
  }

  // Floor
  for (int i = FLOOR_Y; i < SCREEN_HEIGHT; i++) {
    DrawLine(screen, 0, i, SCREEN_WIDTH, 1, 0, brazowy);
  }

  // Draw floor grid lines for perspective
  for (int x = 0; x < STAGE_WIDTH; x += 50) {
    int screenX = x - (int)cameraX;
    if (screenX >= -50 && screenX < SCREEN_WIDTH + 50) {
      DrawLine(screen, screenX, FLOOR_Y, FLOOR_HEIGHT, 0, 1, szary);
    }
  }

  // Stage boundaries - darker lines
  int leftBound = 0 - (int)cameraX;
  int rightBound = STAGE_WIDTH - (int)cameraX;
  if (leftBound >= 0 && leftBound < SCREEN_WIDTH) {
    DrawLine(screen, leftBound, FLOOR_Y, FLOOR_HEIGHT, 0, 1, czarny);
    DrawLine(screen, leftBound + 1, FLOOR_Y, FLOOR_HEIGHT, 0, 1, czarny);
  }
  if (rightBound >= 0 && rightBound < SCREEN_WIDTH) {
    DrawLine(screen, rightBound, FLOOR_Y, FLOOR_HEIGHT, 0, 1, czarny);
    DrawLine(screen, rightBound - 1, FLOOR_Y, FLOOR_HEIGHT, 0, 1, czarny);
  }
}

// Draw the player character
void DrawPlayer(SDL_Surface *screen, double playerX, double playerY,
                double cameraX, int czarny, int czerwony, int zielony) {
  int screenX = (int)(playerX - cameraX);
  int screenY = (int)playerY;

  // Simple character representation - can be replaced with sprite
  // Body
  DrawRectangle(screen, screenX - 10, screenY - 30, 20, 30, czarny, czerwony);
  // Head
  DrawRectangle(screen, screenX - 8, screenY - 45, 16, 15, czarny, zielony);
  // Arms
  DrawRectangle(screen, screenX - 15, screenY - 25, 5, 15, czarny, czerwony);
  DrawRectangle(screen, screenX + 10, screenY - 25, 5, 15, czarny, czerwony);
  // Legs
  DrawRectangle(screen, screenX - 8, screenY, 6, 10, czarny, czerwony);
  DrawRectangle(screen, screenX + 2, screenY, 6, 10, czarny, czerwony);
}

// Draw HUD with game information
void DrawHUD(SDL_Surface *screen, SDL_Surface *charset, GameState *game,
             double fps, int czerwony, int niebieski, int czarny) {
  char text[128];

  // HUD background
  DrawRectangle(screen, 4, 4, SCREEN_WIDTH - 8, 56, czerwony, niebieski);

  // Title
  sprintf(text, "Beat 'em Up Game");
  DrawString(screen, screen->w / 2 - strlen(text) * 8 / 2, 10, text, charset);

  // Time and position info
  sprintf(text, "Time: %.1lf s | Position: %.0lf | FPS: %.0lf",
          game->worldTime, game->playerX, fps);
  DrawString(screen, 12, 26, text, charset);

  // Controls
  sprintf(text, "ESC-exit | N-new game | Arrows-move");
  DrawString(screen, 12, 42, text, charset);
}

// main
#ifdef __cplusplus
extern "C"
#endif
    int main(int argc, char **argv) {
  int t1, t2, quit, frames, rc;
  double delta, fpsTimer, fps;
  SDL_Event event;
  SDL_Surface *screen, *charset;
  SDL_Texture *scrtex;
  SDL_Window *window;
  SDL_Renderer *renderer;
  GameState game;

  // okno konsoli nie jest widoczne, je�eli chcemy zobaczy�
  // komunikaty wypisywane printf-em trzeba w opcjach:
  // project -> szablon2 properties -> Linker -> System -> Subsystem
  // zmieni� na "Console"
  // console window is not visible, to see the printf output
  // the option:
  // project -> szablon2 properties -> Linker -> System -> Subsystem
  // must be changed to "Console"
  printf("wyjscie printfa trafia do tego okienka\n");
  printf("printf output goes here\n");

  if (SDL_Init(SDL_INIT_EVERYTHING) != 0) {
    printf("SDL_Init error: %s\n", SDL_GetError());
    return 1;
  }

  // tryb pe�noekranowy / fullscreen mode
  //	rc = SDL_CreateWindowAndRenderer(0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP,
  //	                                 &window, &renderer);
  rc = SDL_CreateWindowAndRenderer(SCREEN_WIDTH, SCREEN_HEIGHT, 0, &window,
                                   &renderer);
  if (rc != 0) {
    SDL_Quit();
    printf("SDL_CreateWindowAndRenderer error: %s\n", SDL_GetError());
    return 1;
  };

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
  SDL_RenderSetLogicalSize(renderer, SCREEN_WIDTH, SCREEN_HEIGHT);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

  SDL_SetWindowTitle(window, "Beat 'em Up Game");

  screen = SDL_CreateRGBSurface(0, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 0x00FF0000,
                                0x0000FF00, 0x000000FF, 0xFF000000);

  scrtex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH,
                             SCREEN_HEIGHT);

  // wy��czenie widoczno�ci kursora myszy
  SDL_ShowCursor(SDL_DISABLE);

  // wczytanie obrazka cs8x8.bmp
  charset = SDL_LoadBMP("./cs8x8.bmp");
  if (charset == NULL) {
    printf("SDL_LoadBMP(cs8x8.bmp) error: %s\n", SDL_GetError());
    SDL_FreeSurface(screen);
    SDL_DestroyTexture(scrtex);
    SDL_DestroyWindow(window);
    SDL_DestroyRenderer(renderer);
    SDL_Quit();
    return 1;
  };
  SDL_SetColorKey(charset, true, 0x000000);

  char text[128];
  int czarny = SDL_MapRGB(screen->format, 0x00, 0x00, 0x00);
  int zielony = SDL_MapRGB(screen->format, 0x00, 0xFF, 0x00);
  int czerwony = SDL_MapRGB(screen->format, 0xFF, 0x00, 0x00);
  int niebieski = SDL_MapRGB(screen->format, 0x11, 0x11, 0xCC);
  int szary = SDL_MapRGB(screen->format, 0x80, 0x80, 0x80);
  int brazowy = SDL_MapRGB(screen->format, 0x8B, 0x4F, 0x2A);

  // Initialize game
  InitGame(&game);

  t1 = SDL_GetTicks();

  frames = 0;
  fpsTimer = 0;
  fps = 0;
  quit = 0;

  while (!quit) {
    t2 = SDL_GetTicks();

    // w tym momencie t2-t1 to czas w milisekundach,
    // jaki uplyna� od ostatniego narysowania ekranu
    // delta to ten sam czas w sekundach
    // here t2-t1 is the time in milliseconds since
    // the last screen was drawn
    // delta is the same time in seconds
    delta = (t2 - t1) * 0.001;
    t1 = t2;

    if (game.gameActive) {
      game.worldTime += delta;

      // Update player position based on input
      double dx = 0, dy = 0;
      if (game.moveLeft)
        dx -= PLAYER_SPEED * delta;
      if (game.moveRight)
        dx += PLAYER_SPEED * delta;
      if (game.moveUp)
        dy -= PLAYER_SPEED * delta;
      if (game.moveDown)
        dy += PLAYER_SPEED * delta;

      // Update player position with stage boundaries
      game.playerX += dx;
      game.playerY += dy;

      // Clamp player to stage boundaries
      if (game.playerX < 20)
        game.playerX = 20;
      if (game.playerX > STAGE_WIDTH - 20)
        game.playerX = STAGE_WIDTH - 20;

      // Clamp player to floor area
      if (game.playerY < FLOOR_Y)
        game.playerY = FLOOR_Y;
      if (game.playerY > SCREEN_HEIGHT - 20)
        game.playerY = SCREEN_HEIGHT - 20;

      // Update camera to follow player with grace space
      double targetCameraX = game.playerX - SCREEN_WIDTH / 2;
      if (targetCameraX < 0)
        targetCameraX = 0;
      if (targetCameraX > STAGE_WIDTH - SCREEN_WIDTH)
        targetCameraX = STAGE_WIDTH - SCREEN_WIDTH;

      // Smooth camera movement
      double cameraDiff = targetCameraX - game.cameraX;
      game.cameraX += cameraDiff * 5.0 * delta; // Smooth follow
    }

    // Clear screen and draw game
    SDL_FillRect(screen, NULL, czarny);

    if (game.gameActive) {
      DrawStage(screen, game.cameraX, czarny, zielony, szary, brazowy);
      DrawPlayer(screen, game.playerX, game.playerY, game.cameraX, czarny,
                 czerwony, zielony);
    } else {
      // Game over or not started screen
      DrawRectangle(screen, SCREEN_WIDTH / 2 - 150, SCREEN_HEIGHT / 2 - 50,
                    300, 100, czerwony, niebieski);
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
    };

    // Draw HUD
    DrawHUD(screen, charset, &game, fps, czerwony, niebieski, czarny);

    SDL_UpdateTexture(scrtex, NULL, screen->pixels, screen->pitch);
    //		SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, scrtex, NULL, NULL);
    SDL_RenderPresent(renderer);

    // obs�uga zdarze� (o ile jakie� zasz�y) / handling of events (if there were
    // any)
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_KEYDOWN:
        if (event.key.keysym.sym == SDLK_ESCAPE)
          quit = 1;
        else if (event.key.keysym.sym == SDLK_n ||
                 event.key.keysym.sym == SDLK_n)
          InitGame(&game);
        else if (event.key.keysym.sym == SDLK_LEFT)
          game.moveLeft = 1;
        else if (event.key.keysym.sym == SDLK_RIGHT)
          game.moveRight = 1;
        else if (event.key.keysym.sym == SDLK_UP)
          game.moveUp = 1;
        else if (event.key.keysym.sym == SDLK_DOWN)
          game.moveDown = 1;
        break;
      case SDL_KEYUP:
        if (event.key.keysym.sym == SDLK_LEFT)
          game.moveLeft = 0;
        else if (event.key.keysym.sym == SDLK_RIGHT)
          game.moveRight = 0;
        else if (event.key.keysym.sym == SDLK_UP)
          game.moveUp = 0;
        else if (event.key.keysym.sym == SDLK_DOWN)
          game.moveDown = 0;
        break;
      case SDL_QUIT:
        quit = 1;
        break;
      };
    };
    frames++;
  };

  // zwolnienie powierzchni / freeing all surfaces
  SDL_FreeSurface(charset);
  SDL_FreeSurface(screen);
  SDL_DestroyTexture(scrtex);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);

  SDL_Quit();
  return 0;
};
