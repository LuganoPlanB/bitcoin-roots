// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Compiled only as a test module. Stop the owned process at an exact path
// selected by the controller, before the actual filesystem commit operation.
static void boundary(const char* operation, const char* path)
{
    const char* target = getenv("ROOTS_GUI_TEST_CRASH_TARGET");
    const char* expected = getenv("ROOTS_GUI_TEST_CRASH_SYSCALL");
    const char* marker = getenv("ROOTS_GUI_TEST_CRASH_MARKER");
    if (!target || !expected || !marker || !path || strcmp(target, path) || strcmp(expected, operation)) return;
    const int fd = open(marker, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0 || write(fd, "boundary\n", 9) != 9) _exit(88);
    close(fd);
    kill(getpid(), SIGSTOP);
    _exit(89);
}

int unlink(const char* path)
{
    boundary("unlink", path);
    int (*real_unlink)(const char*) = dlsym(RTLD_NEXT, "unlink");
    return real_unlink(path);
}

int rename(const char* before, const char* after)
{
    boundary("rename", after);
    int (*real_rename)(const char*, const char*) = dlsym(RTLD_NEXT, "rename");
    return real_rename(before, after);
}
