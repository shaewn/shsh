#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <limits.h>
#include <unistd.h>

#include "mem_arena.h"
#include "invokers.h"

typedef int (*invoker_t)(int argc, const char **argv);

struct cmd_action {
    invoker_t invoker;
};

struct cmd_node {
    struct cmd_node *next;
    struct cmd_action action;
    int name_count;
    const char **names;
};

struct mem_arena arena;
struct cmd_node *first_node;

#define ALLOC(bytes) mem_arena_alloc(&arena, bytes)
#define FREE(ptr) mem_arena_free(&arena, ptr)

void add_builtin(invoker_t invoker, const char *first_name, ...) {
    va_list more_names;

    va_start(more_names, first_name);

    int name_count = 0;
    const char *current_name = first_name;

    while (current_name) {
        name_count++;
        current_name = va_arg(more_names, const char *);
    }

    va_end(more_names);

    struct cmd_node *node = ALLOC(sizeof(*node));
    node->next = first_node;
    first_node = node;
    node->action.invoker = invoker;
    node->name_count = name_count;
    node->names = ALLOC(name_count * sizeof(*node->names));

    va_start(more_names, first_name);

    current_name = first_name;

    int i = 0;

    while (current_name) {
        node->names[i++] = current_name;
        current_name = va_arg(more_names, const char *);
    }

    va_end(more_names);
}

void init_builtins(void) {
    add_builtin(invoker_pwd, "pwd", NULL);
}

#define PROMPT_MAX 128
#define INPUT_LINE_MAX 4096

void build_argument_vector(const char *command_line, int *argc, char ***argv) {
    const char *s = command_line, *prev;

    int count = 0;
    int cap = 16;
    char **args = malloc(sizeof(*args) * cap);

    // No tokenization yet.
    while (1) {
        while (isspace(*s)) {
            ++s;
        }

        prev = s;

        while (!isspace(*s) && *s) {
            ++s;
        }

        size_t len = s - prev;

        char *ptr = NULL;

        if (len) {
            ptr = ALLOC(len + 1);
            memcpy(ptr, prev, len);
            ptr[len] = 0;
        }

        if (++count > cap) {
            cap *= 2;
            args = realloc(args, sizeof(*args) * cap);
        }

        args[count - 1] = ptr;

        if (len == 0) {
            break;
        }
    }

    args[count] = NULL;

    *argv = ALLOC(count * sizeof(*args));

    for (int i = 0; i < count; i++) {
        (*argv)[i] = args[i];
    }

    free(args);

    *argc = count - 1;
}

int resolve_and_execute_vector(int argc, char **argv) {
    if (!*argv) {
        return -1;
    }

    for (struct cmd_node *n = first_node; n; n = n->next) {
        for (int i = 0; i < n->name_count; i++) {
            if (strcmp(n->names[i], *argv) == 0) {
                return n->action.invoker(argc, (const char **)argv);
            }
        }
    }

    return -1;
}

void do_repl(void) {
    char (*hostname)[HOST_NAME_MAX + 1], (*prompt)[PROMPT_MAX + 1], (*line)[INPUT_LINE_MAX];

    hostname = ALLOC(sizeof(*hostname));
    prompt = ALLOC(sizeof(*prompt));
    line = ALLOC(sizeof(*line));
    while (1) {
        gethostname(*hostname, sizeof(*hostname));

        snprintf(*prompt, sizeof(*prompt), "%s$ ", *hostname);

        write(STDOUT_FILENO, prompt, strlen(*prompt));

        ssize_t count = read(STDIN_FILENO, *line, sizeof(*line));

        if (count == 0 || **line == 0) {
            break;
        }

        struct mem_arena_mark *mark = mem_arena_create_mark(&arena);

        int argc;
        char **argv;
        build_argument_vector(*line, &argc, &argv);

        int result = resolve_and_execute_vector(argc, argv);

        if (result != 0) {
            const char *s = "shsh: error\n";
            write(STDOUT_FILENO, s, strlen(s));
        }

        mem_arena_reset_to_mark(&arena, mark);
        mem_arena_free_mark(&arena, mark);
    }
}

int main(int argc, char **argv) {
    init_mem_arena(&arena);

    init_builtins();

    do_repl();

    deinit_mem_arena(&arena);
}
