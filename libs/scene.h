#ifndef SCENE_H
#define SCENE_H

#include "raylib.h"
#include <stdbool.h>

/* A small scene graph with the component set the engine always understands:
   name/tag, parent, transform (position, rotation, scale), visibility/layer,
   an optional sprite and an optional circle collider.  Game-specific state
   lives in the game module; this is the data the editor can create, inspect,
   undo, save and load without knowing anything about bullets. */

#define SCENE_MAX_ENTITIES 256
#define SCENE_NAME_MAX     64
#define SCENE_TAG_MAX      32
#define SCENE_SPRITE_MAX   96
#define SCENE_VERSION      1

typedef struct {
    char name[SCENE_NAME_MAX];
    char tag[SCENE_TAG_MAX];
    int parent;                 /* -1 = root */
    Vector2 position;
    float rotation;             /* degrees */
    Vector2 scale;
    bool visible;
    int layer;

    bool has_sprite;
    char sprite[SCENE_SPRITE_MAX];
    Vector2 sprite_size;
    Color tint;

    bool has_collider;
    float collider_radius;
    bool collider_trigger;
} Entity;

typedef struct {
    char name[SCENE_NAME_MAX];
    Entity items[SCENE_MAX_ENTITIES];
    int count;
} Scene;

void SceneInit(Scene *s, const char *name);
void SceneClear(Scene *s);
/* A useful starting arena: an Arena marker and a Player Spawn marker. */
void SceneDefault(Scene *s);

int  SceneAdd(Scene *s, const char *name);
int  SceneInsert(Scene *s, int index, const char *name);
void SceneRemove(Scene *s, int index);
int  SceneDuplicate(Scene *s, int index);
int  SceneFind(const Scene *s, const char *name);
int  SceneFindTag(const Scene *s, const char *tag);

Entity *SceneEntity(Scene *s, int index);
const Entity *SceneEntityConst(const Scene *s, int index);

bool SceneSetParent(Scene *s, int child, int parent);
int  SceneChildCount(const Scene *s, int parent);
int  SceneChildAt(const Scene *s, int parent, int nth);

Vector2 SceneWorldPosition(const Scene *s, int index);
float   SceneWorldRotation(const Scene *s, int index);
Vector2 SceneWorldScale(const Scene *s, int index);

bool SceneSave(const Scene *s, const char *path);
bool SceneLoad(Scene *s, const char *path, char *err, int errSize);

#endif
