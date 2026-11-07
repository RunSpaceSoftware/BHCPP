#include "patterns_io.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEG2RAD_F (3.14159265358979323846f / 180.0f)
#define RAD2DEG_F (180.0f / 3.14159265358979323846f)

/* Order must match the enums in patterns.h */
static const char *const ORIGIN_NAMES[] = {
    "edge", "top", "bottom", "left", "right", "point", "player"
};
static const char *const AIM_NAMES[] = {
    "player", "angle", "radial", "wall", "rain", "crossfire", "encircle", "cross", "spear", "formation"
};
static const char *const SIDE_NAMES[] = { "top", "bottom", "left", "right" };

static const struct { const char *name; Color color; } COLOR_NAMES[] = {
    { "white",  { 255, 255, 255, 255 } },
    { "black",  {   0,   0,   0, 255 } },
    { "yellow", { 255, 255,   0, 255 } },
    { "gold",   { 255, 200, 120, 255 } },
    { "orange", { 255, 165,   0, 255 } },
    { "red",    { 255,  60,  60, 255 } },
    { "pink",   { 255, 140, 200, 255 } },
    { "purple", { 200, 120, 255, 255 } },
    { "blue",   { 120, 170, 255, 255 } },
    { "cyan",   { 120, 240, 255, 255 } },
    { "green",  { 120, 255, 150, 255 } },
    { "grey",   { 160, 160, 160, 255 } },
};

/* ------------------------------------------------------------------ text */
const char *OriginName(Origin origin)
{
    if ((int)origin < 0 || (int)origin >= (int)(sizeof ORIGIN_NAMES / sizeof *ORIGIN_NAMES)) return "edge";
    return ORIGIN_NAMES[origin];
}

const char *AimName(Aim aim)
{
    if ((int)aim < 0 || (int)aim >= (int)(sizeof AIM_NAMES / sizeof *AIM_NAMES)) return "player";
    return AIM_NAMES[aim];
}

const char *SideName(Side side)
{
    if ((int)side < 0 || (int)side >= (int)(sizeof SIDE_NAMES / sizeof *SIDE_NAMES)) return "top";
    return SIDE_NAMES[side];
}

static int name_to_index(const char *const *names, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (strcmp(names[i], name) == 0) return i;
    return -1;
}

int OriginFromName(const char *name)
{
    return name_to_index(ORIGIN_NAMES, (int)(sizeof ORIGIN_NAMES / sizeof *ORIGIN_NAMES), name);
}

int AimFromName(const char *name)
{
    return name_to_index(AIM_NAMES, (int)(sizeof AIM_NAMES / sizeof *AIM_NAMES), name);
}

int SideFromName(const char *name)
{
    return name_to_index(SIDE_NAMES, (int)(sizeof SIDE_NAMES / sizeof *SIDE_NAMES), name);
}

/* -------------------------------------------------------------- parsing */
static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_float(const char *s, float *out)
{
    char *end = NULL;
    float v = strtof(s, &end);
    if (end == s) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return false;
    *out = v;
    return true;
}

static bool parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return false;
    *out = (int)v;
    return true;
}

static unsigned char clamp_byte(int v)
{
    return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static bool parse_color(const char *s, Color *out)
{
    for (int i = 0; i < (int)(sizeof COLOR_NAMES / sizeof *COLOR_NAMES); i++) {
        if (strcmp(COLOR_NAMES[i].name, s) == 0) {
            *out = COLOR_NAMES[i].color;
            return true;
        }
    }

    int r = 0, g = 0, b = 0, a = 255;
    int got = sscanf(s, "%d , %d , %d , %d", &r, &g, &b, &a);
    if (got < 3) return false;

    out->r = clamp_byte(r);
    out->g = clamp_byte(g);
    out->b = clamp_byte(b);
    out->a = (got >= 4) ? clamp_byte(a) : (unsigned char)255;
    return true;
}

/* --------------------------------------------------------------- loading */
int PatternsLoad(const char *path, PatternSet *set, char *err, int errSize)
{
    if (!path || !set) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errSize, "cannot open '%s'", path);
        return -1;
    }

    char line[1024];
    int lineNo = 0;
    int count = 0;
    bool inBlock = false;
    int blockLine = 0;
    Pattern cur;

    memset(&cur, 0, sizeof cur);
    memset(err, 0, errSize);

    while (fgets(line, sizeof line, f)) {
        lineNo++;

        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *s = trim(line);
        if (!*s) continue;

        if (!inBlock) {
            if (strncmp(s, "pattern", 7) == 0 && (s[7] == '\0' || isspace((unsigned char)s[7]))) {
                char *name = trim(s + 7);
                if (!*name) {
                    snprintf(err, errSize, "line %d: 'pattern' needs a name", lineNo);
                    goto fail;
                }
                if (strlen(name) >= PATTERN_NAME_MAX) {
                    snprintf(err, errSize, "line %d: name longer than %d characters", lineNo, PATTERN_NAME_MAX - 1);
                    goto fail;
                }
                if (count >= MAX_PATTERNS) {
                    snprintf(err, errSize, "line %d: more than %d patterns", lineNo, MAX_PATTERNS);
                    goto fail;
                }

                memset(&cur, 0, sizeof cur);
                snprintf(set->names[count], PATTERN_NAME_MAX, "%s", name);
                cur.name = set->names[count];
                inBlock = true;
                blockLine = lineNo;
            } else {
                snprintf(err, errSize, "line %d: expected 'pattern <name>' but found '%s'", lineNo, s);
                goto fail;
            }
            continue;
        }

        if (strcmp(s, "end") == 0) {
            set->items[count] = pattern_apply_defaults(cur);
            count++;
            inBlock = false;
            continue;
        }

        char *key = s;
        char *value = s;
        while (*value && !isspace((unsigned char)*value)) value++;
        if (*value) {
            *value++ = '\0';
            value = trim(value);
        }
        if (!*value) {
            snprintf(err, errSize, "line %d: '%s' needs a value", lineNo, key);
            goto fail;
        }

        bool ok = true;
        float fv = 0.0f;
        int iv = 0;

        if (strcmp(key, "origin") == 0) {
            int v = OriginFromName(value);
            ok = (v >= 0);
            cur.origin = (Origin)(ok ? v : 0);
        } else if (strcmp(key, "aim") == 0) {
            int v = AimFromName(value);
            ok = (v >= 0);
            cur.aim = (Aim)(ok ? v : 0);
        } else if (strcmp(key, "side") == 0) {
            int v = SideFromName(value);
            ok = (v >= 0);
            cur.side = (Side)(ok ? v : 0);
        } else if (strcmp(key, "count") == 0) {
            ok = parse_int(value, &iv);
            cur.count = iv;
        } else if (strcmp(key, "spread") == 0) {
            ok = parse_float(value, &fv);
            cur.spread = fv * DEG2RAD_F;
        } else if (strcmp(key, "speed") == 0) {
            ok = parse_float(value, &fv);
            cur.speed = fv;
        } else if (strcmp(key, "delay") == 0) {
            ok = parse_float(value, &fv);
            cur.delay = fv;
        } else if (strcmp(key, "repeat") == 0) {
            ok = parse_int(value, &iv);
            cur.repeat = iv;
        } else if (strcmp(key, "damage") == 0) {
            ok = parse_int(value, &iv);
            cur.damage = iv;
        } else if (strcmp(key, "radius") == 0) {
            ok = parse_float(value, &fv);
            cur.radius = fv;
        } else if (strcmp(key, "spin") == 0) {
            ok = parse_float(value, &fv);
            cur.spin = fv * DEG2RAD_F;
        } else if (strcmp(key, "jitter") == 0) {
            ok = parse_float(value, &fv);
            cur.jitter = fv * DEG2RAD_F;
        } else if (strcmp(key, "angle") == 0) {
            ok = parse_float(value, &fv);
            cur.angle = fv * DEG2RAD_F;
            cur.has_angle = ok;
        } else if (strcmp(key, "x") == 0) {
            ok = parse_float(value, &fv);
            cur.point.x = fv;
        } else if (strcmp(key, "y") == 0) {
            ok = parse_float(value, &fv);
            cur.point.y = fv;
        } else if (strcmp(key, "gap") == 0) {
            if (strcmp(value, "none") == 0) {
                cur.gap = 0;
            } else if (strcmp(value, "random") == 0) {
                cur.gap = GAP_RANDOM;
            } else if (parse_int(value, &iv) && iv >= 0) {
                cur.gap = GAP_AT(iv);
            } else {
                ok = false;
            }
        } else if (strcmp(key, "spawn_radius") == 0) {
            ok = parse_float(value, &fv);
            cur.spawn_radius = fv;
        } else if (strcmp(key, "min_radius") == 0) {
            ok = parse_float(value, &fv);
            cur.min_radius = fv;
        } else if (strcmp(key, "segments") == 0) {
            ok = parse_int(value, &iv);
            cur.segments = iv;
        } else if (strcmp(key, "length") == 0) {
            ok = parse_float(value, &fv);
            cur.length = fv;
        } else if (strcmp(key, "color") == 0) {
            ok = parse_color(value, &cur.color);
        } else {
            ok = false;
        }

        if (!ok) {
            snprintf(err, errSize, "line %d: bad value '%s' for '%s'", lineNo, value, key);
            goto fail;
        }
    }

    if (inBlock) {
        snprintf(err, errSize, "line %d: pattern has no 'end'", blockLine);
        goto fail;
    }
    if (count == 0) {
        snprintf(err, errSize, "'%s' contains no patterns", path);
        goto fail;
    }

    fclose(f);
    return count;

fail:
    fclose(f);
    return -1;
}

/* --------------------------------------------------------------- saving */
bool PatternsSave(const char *path, const Pattern *list, int count)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;

    fprintf(f, "# Bullet Hell pattern data\n");
    fprintf(f, "# Written by tools/pattern_editor - you can also edit this by hand.\n");
    fprintf(f, "# Angles (spread/spin/jitter/angle) are in degrees; '#' starts a comment.\n");
    fprintf(f, "# Any key may be left out - the game then uses its built-in default.\n\n");

    for (int i = 0; i < count; i++) {
        Pattern p = pattern_apply_defaults(list[i]);

        fprintf(f, "pattern %s\n", (p.name && *p.name) ? p.name : "unnamed");
        fprintf(f, "  origin %s\n", OriginName(p.origin));
        fprintf(f, "  aim %s\n", AimName(p.aim));
        if (p.aim == AIM_WALL) fprintf(f, "  side %s\n", SideName(p.side));
        if (p.origin == ORIGIN_POINT) fprintf(f, "  x %g\n  y %g\n", p.point.x, p.point.y);

        fprintf(f, "  count %d\n", p.count);
        fprintf(f, "  spread %g\n", p.spread * RAD2DEG_F);
        fprintf(f, "  speed %g\n", p.speed);
        fprintf(f, "  delay %g\n", p.delay);
        fprintf(f, "  repeat %d\n", p.repeat);
        fprintf(f, "  damage %d\n", p.damage);
        fprintf(f, "  radius %g\n", p.radius);

        if (p.spin != 0.0f) fprintf(f, "  spin %g\n", p.spin * RAD2DEG_F);
        if (p.jitter != 0.0f) fprintf(f, "  jitter %g\n", p.jitter * RAD2DEG_F);
        if (p.has_angle) fprintf(f, "  angle %g\n", p.angle * RAD2DEG_F);

        if (p.gap != 0 && (p.aim == AIM_WALL || p.aim == AIM_ENCIRCLE || p.aim == AIM_CROSS)) {
            if (p.gap == GAP_RANDOM) fprintf(f, "  gap random\n");
            else fprintf(f, "  gap %d\n", p.gap - 1);
        }

        if (p.aim == AIM_ENCIRCLE || p.aim == AIM_CROSS) {
            fprintf(f, "  spawn_radius %g\n", p.spawn_radius);
            fprintf(f, "  min_radius %g\n", p.min_radius);
        }
        if (p.aim == AIM_SPEAR || p.aim == AIM_FORMATION) {
            if (p.aim == AIM_FORMATION) fprintf(f, "  segments %d\n", p.segments);
            fprintf(f, "  length %g\n", p.length);
        }

        fprintf(f, "  color %d,%d,%d\n", p.color.r, p.color.g, p.color.b);
        fprintf(f, "end\n\n");
    }

    fclose(f);
    return true;
}

/* ---------------------------------------------------------- runtime table */
static PatternSet g_set;
static int        g_count = 0;
static bool       g_tried = false;
static char       g_path[4096] = { 0 };

static bool load_runtime(void)
{
    char paths[3][4096];
    const char *app = GetApplicationDirectory();

    snprintf(paths[0], sizeof paths[0], "%sresources/patterns.txt", app);
    snprintf(paths[1], sizeof paths[1], "%s../resources/patterns.txt", app);
    snprintf(paths[2], sizeof paths[2], "resources/patterns.txt");

    char err[256] = { 0 };
    for (int i = 0; i < 3; i++) {
        if ((i > 0) && (strcmp(paths[i], paths[i - 1]) == 0)) continue;
        if (!FileExists(paths[i])) continue;

        int n = PatternsLoad(paths[i], &g_set, err, sizeof err);
        if (n > 0) {
            g_count = n;
            snprintf(g_path, sizeof g_path, "%.4090s", paths[i]);
            TraceLog(LOG_INFO, "PATTERNS: loaded %d patterns from %s", n, g_path);
            return true;
        }
        TraceLog(LOG_WARNING, "PATTERNS: %s", err);
    }

    TraceLog(LOG_INFO, "PATTERNS: no patterns.txt - using the built-in table");
    return false;
}

const Pattern *PatternsGet(int *count)
{
    if (!g_tried) {
        g_tried = true;
        load_runtime();
    }

    if (g_count > 0) {
        if (count) *count = g_count;
        return g_set.items;
    }

    if (count) *count = PATTERN_COUNT;
    return PATTERNS;
}

const char *PatternsPath(void)
{
    return g_path;
}

bool PatternsReload(void)
{
    g_count = 0;
    g_path[0] = '\0';
    g_tried = true;
    return load_runtime();
}
