#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "invokers.h"
#include "mem_arena.h"

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

int ttyfd = -1;
FILE *shin, *shout;

pid_t fg_pgrp;

void tblock(int sig, int blocked);

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

void init_builtins(void) { add_builtin(invoker_pwd, "pwd", NULL); }

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

void fg_pgrp_sig_handler(int sig) {
    if (fg_pgrp != 0) {
        kill(-fg_pgrp, sig);
    }

    fprintf(shout, "Received signal: %s\n", strsignal(sig));
    fflush(shout);
}

int resolve_and_execute_vector(int argc, char **argv) {
    if (!*argv) {
        return -1;
    }

    // TODO: Could probably use a hash table here.
    for (struct cmd_node *n = first_node; n; n = n->next) {
        for (int i = 0; i < n->name_count; i++) {
            if (strcmp(n->names[i], *argv) == 0) {
                return n->action.invoker(argc, (const char **)argv);
            }
        }
    }

    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    fg_pgrp = child;

    if (child == 0) {
        setpgid(0, 0); // In our own process group.

        tblock(SIGTTOU, 1);

        int result = tcsetpgrp(ttyfd, getpgrp());
        if (result == -1) {
            fprintf(shout, "Failed to become foreground process group (I am %lld)\n", (long long)getpid());
            exit(EXIT_FAILURE);
        }

        tblock(SIGTTOU, 0);

        // Make sure we don't have any lingering messages before we change ourself.
        fflush(shout);

        execvp(*argv, argv);

        switch (errno) {
            case ENOENT: {
                const char *s = *argv;
                while (*s && *s != '/')
                    ++s;
                if (*s == '/') {
                    fprintf(shout, "shsh: no such file or directory: %s\n", *argv);
                } else {
                    fprintf(shout, "shsh: command not found: %s\n", *argv);
                }

                break;
            }
            case EACCES:
            case EPERM:
                fprintf(shout, "shsh: permission denied: %s\n", *argv);
                break;
            default:
                fprintf(shout, "shsh: unhandled error: %s\n", strerror(errno));
                break;
        }

        fflush(shout);

        _exit(1);
    } else {
        int status, result;
        waitpid(child, &status, WUNTRACED);

        tblock(SIGTTOU, 1);

        result = tcsetpgrp(ttyfd, getpgrp());

        if (result == -1) {
            fprintf(shout, "Failed to set ctty fg pgrp to %d\n", getpgrp());
            exit(EXIT_FAILURE);
        }

        tblock(SIGTTOU, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            return -1;
        }

        if (WIFSTOPPED(status)) {
            fprintf(shout, "%lld stopped (%s)\n", (long long)child, strsignal(WSTOPSIG(status)));
            fflush(shout);
            return -1;
        }

        if (WIFSIGNALED(status)) {
            const char *s = NULL;

            switch (WTERMSIG(status)) {
                case SIGKILL: {
                    s = "killed";
                    break;
                }
                case SIGTERM: {
                    s = "terminated";
                    break;
                }

                default: break;
            }

            if (s) {
                fprintf(shout, "%lld %s\n", (long long)child, s);
                fflush(shout);
            }

            return -1;
        }

        return 0;
    }

    return -1;
}

void do_repl(void) {
    // TODO: To be posix compliant (which we're not even close to anyway), the line would need to allow 'unlimited' text.
    char (*hostname)[HOST_NAME_MAX + 1], (*line)[INPUT_LINE_MAX];

    hostname = ALLOC(sizeof(*hostname));
    line = ALLOC(sizeof(*line));
    while (1) {
        gethostname(*hostname, sizeof(*hostname));
        fprintf(shout, "%s$ ", *hostname);
        fflush(shout);

        errno = 0;
        if (!fgets(*line, sizeof(*line), shin)) {
            if (errno != 0) {
                switch (errno) {
                    case EINTR: {
                        continue;
                    }
                    
                    default: break;
                }

                fprintf(shout, "fgets: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            } else {
                break;
            }
        }

        if ((*line)[1] == 0) {
            continue;
        }

        struct mem_arena_mark *mark = mem_arena_create_mark(&arena);

        int argc;
        char **argv;
        build_argument_vector(*line, &argc, &argv);

        int result = resolve_and_execute_vector(argc, argv);

        if (result != 0) {
            fprintf(shout, "shsh: error\n");
            fflush(shout);
        }

        mem_arena_reset_to_mark(&arena, mark);
        mem_arena_free_mark(&arena, mark);
    }
}

void ttxx_handler(int sig) {
    fprintf(stderr, "Received %s\n", strsignal(sig));
    _exit(EXIT_FAILURE);
}

void setup_tty(void) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        exit(EXIT_FAILURE);
    }

#define has_rdwr(fd) ((fcntl((fd), F_GETFL) & O_RDWR) == O_RDWR)

    if (isatty(STDIN_FILENO) && has_rdwr(STDIN_FILENO)) {
        ttyfd = STDIN_FILENO;
    } else if (isatty(STDOUT_FILENO) && has_rdwr(STDOUT_FILENO)) {
        ttyfd = STDOUT_FILENO;
    } else {
        // Can't get rw permissions on a single tty.
        exit(EXIT_FAILURE);
    }

    ttyfd = dup2(ttyfd, 10);
    int fl = fcntl(ttyfd, F_GETFL);
    fcntl(ttyfd, F_SETFL, fl | FD_CLOEXEC);

    shin = fdopen(ttyfd, "r");
    shout = fdopen(ttyfd, "w");

    static char shin_buf[BUFSIZ], shout_buf[BUFSIZ];

    setvbuf(shin, shin_buf, _IOFBF, sizeof(shin_buf));
    setvbuf(shout, shout_buf, _IOFBF, sizeof(shout_buf));

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
}

void setup_signals(void) {
    struct sigaction act;

    act.sa_flags = 0;
    act.sa_handler = fg_pgrp_sig_handler;

    int result = sigaction(SIGINT, &act, NULL);
    if (result == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    sigaction(SIGTSTP, &act, NULL);
    if (result == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    setup_tty();
    setup_signals();

    init_mem_arena(&arena);

    init_builtins();

    do_repl();

    deinit_mem_arena(&arena);
}

void tblock(int sig, int blocked) {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(blocked ? SIG_BLOCK : SIG_UNBLOCK, &mask, NULL);
}
