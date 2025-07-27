#include "invokers.h"

#include <string.h>
#include <unistd.h>
#include <limits.h>

int invoker_pwd(int argc, const char **argv) {
    char buf[PATH_MAX + 1];

    char *res = getcwd(buf, PATH_MAX);
    strcat(buf, "\n");

    if (res) {
        write(STDOUT_FILENO, buf, strlen(buf));
        return 0;
    }

    return 1;
}
