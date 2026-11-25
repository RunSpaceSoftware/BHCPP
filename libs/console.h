#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>

/* In-engine developer console: a bounded log ring plus named commands. The
   editor renders it; the game gets the same log for free. */

#define CONSOLE_MAX_LINES 256
#define CONSOLE_LINE_MAX  512
#define CONSOLE_MAX_COMMANDS 32

typedef enum {
    CONSOLE_INFO = 0,
    CONSOLE_WARN,
    CONSOLE_ERROR,
    CONSOLE_COMMAND,
    CONSOLE_OUTPUT,
} ConsoleLevel;

typedef void (*ConsoleCommandFn)(int argc, char **argv);

void ConsoleInit(void);
void ConsoleShutdown(void);
void ConsoleClear(void);

void ConsolePrint(ConsoleLevel level, const char *fmt, ...);
void ConsolePrintRaw(ConsoleLevel level, const char *text);

int ConsoleLineCount(void);
/* 0 is the oldest line still retained. */
const char *ConsoleLine(int index, ConsoleLevel *level);

bool ConsoleRegister(const char *name, const char *help, ConsoleCommandFn fn);
/* Echo the command, run it, print the result. Returns false for an unknown
   command (after logging it). */
bool ConsoleExec(const char *command);

int ConsoleCommandCount(void);
const char *ConsoleCommandName(int index);
const char *ConsoleCommandHelp(int index);

/* Engine log sink, so EngineLog* messages land in the console. */
void ConsoleLogSink(int level, const char *message);

#endif
