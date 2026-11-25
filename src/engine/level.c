#include "level.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void LevelsClear(LevelSet *set)
{
    if (!set) return;
    memset(set, 0, sizeof *set);
}

bool LevelsDefault(LevelSet *set, const char *const *pattern_names, int count)
{
    if (!set) return false;
    LevelsClear(set);
    Level *level = &set->items[0];
    snprintf(level->name, sizeof level->name, "All Patterns");
    for (int i = 0; i < count && level->pattern_count < LEVEL_MAX_PATTERNS; i++) {
        if (!pattern_names || !pattern_names[i] || !pattern_names[i][0]) continue;
        snprintf(level->patterns[level->pattern_count++], LEVEL_PATTERN_MAX, "%s", pattern_names[i]);
    }
    if (level->pattern_count == 0) return false;
    set->count = 1;
    return true;
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_quoted(const char *s, char *out, int cap)
{
    if (!s || !out || cap <= 0) return false;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s != '"') { snprintf(out, (size_t)cap, "%s", s); return true; }
    s++;
    int n = 0;
    while (*s && *s != '"' && n < cap - 1) out[n++] = *s++;
    out[n] = '\0';
    return true;
}

static void set_error(char *err, int errSize, int line, const char *fmt, ...)
{
    if (!err || errSize <= 0) return;
    char detail[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    snprintf(err, (size_t)errSize, "line %d: %s", line, detail);
}

bool LevelsLoad(const char *path, LevelSet *set, char *err, int errSize)
{
    if (!set || !path) return false;
    if (err && errSize > 0) err[0] = '\0';

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err && errSize > 0) snprintf(err, (size_t)errSize, "cannot open %s", path);
        return false;
    }

    LevelSet tmp;
    LevelsClear(&tmp);
    Level *current = NULL;
    int line_no = 0;
    bool ok = true;
    char raw[1024];

    while (ok && fgets(raw, sizeof raw, f)) {
        line_no++;
        char *line = trim(raw);
        if (!*line || *line == '#' || *line == ';') continue;

        if (strncmp(line, "level", 5) == 0 && isspace((unsigned char)line[5])) {
            if (tmp.count >= LEVEL_MAX_LEVELS) {
                set_error(err, errSize, line_no, "too many levels");
                ok = false;
                break;
            }
            current = &tmp.items[tmp.count++];
            memset(current, 0, sizeof *current);
            parse_quoted(line + 5, current->name, sizeof current->name);
            if (!current->name[0]) snprintf(current->name, sizeof current->name, "Level %d", tmp.count);
            continue;
        }

        if (strcmp(line, "end") == 0) { current = NULL; continue; }

        if (strncmp(line, "pattern", 7) == 0 && isspace((unsigned char)line[7])) {
            if (!current) {
                set_error(err, errSize, line_no, "pattern outside a level");
                ok = false;
                break;
            }
            if (current->pattern_count >= LEVEL_MAX_PATTERNS) {
                set_error(err, errSize, line_no, "too many patterns in a level");
                ok = false;
                break;
            }
            char name[LEVEL_PATTERN_MAX];
            parse_quoted(line + 7, name, sizeof name);
            if (name[0])
                snprintf(current->patterns[current->pattern_count++], LEVEL_PATTERN_MAX, "%s", name);
            continue;
        }

        set_error(err, errSize, line_no, "unknown key '%s'", line);
        ok = false;
    }

    fclose(f);
    if (!ok) return false;
    if (tmp.count == 0) {
        if (err && errSize > 0) snprintf(err, (size_t)errSize, "no levels in file");
        return false;
    }
    *set = tmp;
    return true;
}

static void write_quoted(FILE *f, const char *text)
{
    fputc('"', f);
    for (; text && *text; text++) fputc(*text == '"' ? '\'' : *text, f);
    fputc('"', f);
}

bool LevelsSave(const char *path, const LevelSet *set)
{
    if (!path || !set) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "# %d levels\n\n", set->count);
    for (int i = 0; i < set->count; i++) {
        const Level *l = &set->items[i];
        fprintf(f, "level ");
        write_quoted(f, l->name);
        fprintf(f, "\n");
        for (int p = 0; p < l->pattern_count; p++)
            fprintf(f, "  pattern %s\n", l->patterns[p]);
        fprintf(f, "end\n\n");
    }
    fclose(f);
    return true;
}

int LevelsFind(const LevelSet *set, const char *name)
{
    if (!set || !name) return -1;
    for (int i = 0; i < set->count; i++)
        if (strcmp(set->items[i].name, name) == 0) return i;
    return -1;
}
