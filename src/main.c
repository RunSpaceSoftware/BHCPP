#include "engine.h"
#include "game.h"

#include "raylib.h"

/* Host for the standalone game. All that lives here is the window and frame
   loop - the engine owns startup/teardown and the game module owns the run. */

int main(void)
{
    EngineConfig cfg = EngineDefaultConfig();
    cfg.title = "Bullet Hell";
    cfg.width = 1200;
    cfg.height = 600;
    cfg.min_width = 800;
    cfg.min_height = 480;

    if (!EngineInit(&cfg)) return 1;

    static Game game;
    static Scene scene;
    SceneDefault(&scene);
    char scene_path[4096] = { 0 };
    if (EngineResolveResource("scenes/main.scene", scene_path, sizeof scene_path)) {
        char err[256];
        if (!SceneLoad(&scene, scene_path, err, sizeof err))
            EngineLogWarn("MAIN: %s", err);
    }

    if (!GameInit(&game, EngineWidth(), EngineHeight())) {
        EngineShutdown();
        return 1;
    }
    GameSetScene(&game, &scene);
    GameReset(&game);

    while (EngineBeginFrame()) {
        if (GameWantsQuit(&game)) break;

        int w = EngineWidth();
        int h = EngineHeight();
        if (w != game.width || h != game.height) GameSetSize(&game, w, h);

        if (IsKeyPressed(KEY_F5)) {
            GameReload(&game);
            if (scene_path[0]) {
                char err[256];
                if (!SceneLoad(&scene, scene_path, err, sizeof err)) EngineLogWarn("MAIN: %s", err);
            }
        }

        GameUpdate(&game, EngineDeltaTime());

        BeginDrawing();
        ClearBackground(BLACK);
        GameDraw(&game);
        EndDrawing();
    }

    GameShutdown(&game);
    EngineShutdown();
    return 0;
}