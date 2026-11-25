#ifndef GAME_H
#define GAME_H

#include "bullet.h"
#include "level.h"
#include "pause_menu.h"
#include "patterns.h"
#include "player_stats.h"
#include "raylib.h"
#include "scene.h"
#include <stdbool.h>

/* Bullet Hell - the game client. It owns the run, menus and scoring rules;
   the engine owns time, input, assets, audio, scenes and the editor. The
   client is windowless: GameDraw() paints into whatever render target is
   current, so the standalone host draws it to the screen and the editor
   draws it into a viewport render texture for play-in-editor. */

typedef enum {
    GAME_SCREEN_MENU = 0,
    GAME_SCREEN_PLAY,
    GAME_SCREEN_PAUSE
} GameScreen;

typedef struct {
    int width, height;
    GameScreen screen;

    PauseMenu pause_menu;
    int pause_sel;
    bool pause_run;

    PlayerStats stats;
    float speed;
    float pos_x, pos_y;
    int hp;
    float invuln;

    BulletList bullets;
    Pattern active[MAX_PATTERNS];
    int active_count;
    int pattern_index;
    int shots_left;
    float shot_timer;
    SpinState spin_state;
    bool warn_pending;

    const Pattern *patterns;
    int table_count;
    LevelSet levels;
    bool has_levels;
    int level_index;

    int menu_sel;
    int diff_index;
    bool quit_requested;

    Texture2D player_tex;
    bool have_tex;

    char status_text[160];
    bool status_ok;
    float status_timer;

    /* play-in-editor mouse mapping: screen point -> game point */
    Vector2 mouse_origin;
    float mouse_scale;

    const Scene *scene;   /* optional: player_spawn tag positions the player */
} Game;

/* width/height are the logical arena size (1200x600 for the standalone
   game). Loads stats, the pause menu, patterns and the player sprite. */
bool GameInit(Game *g, int width, int height);
void GameShutdown(Game *g);

void GameSetSize(Game *g, int width, int height);
void GameSetScene(Game *g, const Scene *scene);
void GameSetMouseTransform(Game *g, Vector2 origin, float scale);

/* Reset to a fresh run and enter play. GameToMenu returns to the title. */
void GameReset(Game *g);
void GameToMenu(Game *g);

void GameUpdate(Game *g, float dt);
void GameDraw(Game *g);

/* Re-read patterns.txt, scripts, player.txt, pause.txt, levels and sprites
   without restarting. Returns false when patterns.txt had an error (the
   previous table is kept, matching the F5 behaviour). */
bool GameReload(Game *g);

/* Optional level file; falls back to one level containing every pattern. */
void GameLoadLevels(Game *g, const char *path);

bool GameWantsQuit(const Game *g);
const char *GameCurrentPattern(const Game *g);

#endif
