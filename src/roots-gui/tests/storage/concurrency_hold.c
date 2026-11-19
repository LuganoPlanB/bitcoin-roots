// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Linux fixture only. Pause once when the real file reader has validated and
// read the selected disposable file, before its parser receives those bytes.
int close(int fd)
{
    int (*real_close)(int) = dlsym(RTLD_NEXT, "close");
    const char* target = getenv("ROOTS_GUI_TEST_HELD_FILE");
    const char* entered = getenv("ROOTS_GUI_TEST_FILE_ENTERED");
    const char* release = getenv("ROOTS_GUI_TEST_FILE_RELEASE");
    static atomic_int used;
    if (target && entered && release && !atomic_load(&used)) {
        char name[64], path[4096];
        snprintf(name, sizeof(name), "/proc/self/fd/%d", fd);
        const ssize_t size = readlink(name, path, sizeof(path) - 1);
        if (size >= 0) {
            path[size] = 0;
            if (!strcmp(target, path) && !atomic_exchange(&used, 1)) {
                const int marker = open(entered, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
                if (marker < 0 || real_close(marker) != 0) _exit(86);
                struct timespec pause = {0, 2000000};
                for (unsigned attempt = 0; access(release, F_OK) != 0; ++attempt) {
                    if (attempt == 2500) _exit(87);
                    nanosleep(&pause, NULL);
                }
            }
        }
    }
    return real_close(fd);
}
