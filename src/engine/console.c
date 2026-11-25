#include "console.h"
#include "engine.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char text[CONSOLE_LINE_MAX];
    ConsoleLevel level;
} ConsoleLineEntry;

typedef struct {
    char name[32];
    char help[160];
    ConsoleCommandFn fn;
} ConsoleCommand;

static ConsoleLineEntry g_lines[CONSOLE_MAX_LINES];
static int g_count = 0;
static int g_start = 0;

static ConsoleCommand g_commands[CONSOLE_MAX_COMMANDS];
static int g_command_count = 0;

static void push_line(ConsoleLevel level, const char *text)
{
    int slot;
    if (g_count < CONSOLE_MAX_LINES) {
        slot = (g_start + g_count) % CONSOLE_MAX_LINES;
        g_count++;
    } else {
        slot = g_start;
        g_start = (g_start + 1) % CONSOLE_MAX_LINES;
    }
    snprintf(g_lines[slot].text, sizeof g_lines[slot].text, "%s", text ? text : "");
    g_lines[slot].level = level;
}

void ConsoleInit(void)
{
    g_count = 0;
    g_start = 0;
    g_command_count = 0;
    EngineSetLogSink(ConsoleLogSink);
    ConsolePrint(CONSOLE_OUTPUT, "%s %s console - type 'help'", ENGINE_NAME, ENGINE_VERSION);
}

void ConsoleShutdown(void)
{
    EngineSetLogSink(NULL);
    g_count = 0;
    g_start = 0;
    g_command_count = 0;
}

void ConsoleClear(void) { g_count = 0; g_start = 0; }

void ConsolePrintRaw(ConsoleLevel level, const char *text)
{
    if (!text) return;
    /* split very long output so one printf cannot silently eat the rest */
    const char *p = text;
    while (*p) {
        char line[CONSOLE_LINE_MAX];
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = '\0';
        push_line(level, line);
        if (!nl) break;
        p = nl + 1;
    }
}

void ConsolePrint(ConsoleLevel level, const char *fmt, ...)
{
    char text[CONSOLE_LINE_MAX];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    ConsolePrintRaw(level, text);
}

int ConsoleLineCount(void) { return g_count; }

const char *ConsoleLine(int index, ConsoleLevel *level)
{
    if (index < 0 || index >= g_count) return "";
    int slot = (g_start + index) % CONSOLE_MAX_LINES;
    if (level) *level = g_lines[slot].level;
    return g_lines[slot].text;
}

void ConsoleLogSink(int level, const char *message)
{
    ConsoleLevel l = CONSOLE_INFO;
    if (level == ENGINE_LOG_WARN) l = CONSOLE_WARN;
    else if (level == ENGINE_LOG_ERROR) l = CONSOLE_ERROR;
    ConsolePrintRaw(l, message);
}

bool ConsoleRegister(const char *name, const char *help, ConsoleCommandFn fn)
{
    if (!name || !*name || !fn) return false;
    if (g_command_count >= CONSOLE_MAX_COMMANDS) return false;
    ConsoleCommand *c = &g_commands[g_command_count++];
    snprintf(c->name, sizeof c->name, "%s", name);
    snprintf(c->help, sizeof c->help, "%s", help ? help : "");
    c->fn = fn;
    return true;
}

int ConsoleCommandCount(void) { return g_command_count; }
const char *ConsoleCommandName(int index)
{
    return (index >= 0 && index < g_command_count) ? g_commands[index].name : "";
}
const char *ConsoleCommandHelp(int index)
{
    return (index >= 0 && index < g_command_count) ? g_commands[index].help : "";
}

static int split_args(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            argv[argc++] = p;
            char *out = p;
            while (*p && *p != '"') *out++ = *p++;
            *out = '\0';
            if (*p == '"') p++;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    return argc;
}

static void command_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    ConsolePrint(CONSOLE_OUTPUT, "commands:");
    for (int i = 0; i < g_command_count; i++)
        ConsolePrint(CONSOLE_OUTPUT, "  %-10s %s", g_commands[i].name, g_commands[i].help);
}

bool ConsoleExec(const char *command)
{
    if (!command || !*command) return true;
    char buffer[CONSOLE_LINE_MAX];
    snprintf(buffer, sizeof buffer, "%s", command);
    ConsolePrint(CONSOLE_COMMAND, "> %s", buffer);

    char *argv[16];
    int argc = split_args(buffer, argv, 16);
    if (argc == 0) return true;

    if (strcmp(argv[0], "help") == 0) { command_help(argc, argv); return true; }

    for (int i = 0; i < g_command_count; i++) {
        if (strcmp(g_commands[i].name, argv[0]) == 0) {
            g_commands[i].fn(argc, argv);
            return true;
        }
    }
    ConsolePrint(CONSOLE_ERROR, "unknown command '%s' - type 'help'", argv[0]);
    return false;
}
