#ifndef SHELL_COMMAND_PARSER_H_
#define SHELL_COMMAND_PARSER_H_

#include "mem_arena.h"

typedef struct input_stream {
    void *ctx;
    int (*getch)(void *);
} InputStream;

typedef struct command_parser {
    InputStream input;
    MemArena *arena;
} CommandParser;

typedef struct simple_command {
} SimpleCommand;

typedef struct command_list {
} CommandList;

typedef struct parsed_command {
} ParsedCommand;

void command_parser_init(CommandParser *cp);

ParsedCommand *parse_command(CommandParser *cp);

#endif
