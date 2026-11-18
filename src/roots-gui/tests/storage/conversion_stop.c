// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
// Test-only process stop at the real plaintext retirement boundary. The target
// must be the runner's exact disposable pathname; no production hook exists.
static void boundary(const char* path){const char* target=getenv("ROOTS_GUI_STOP_UNLINK");if(target && path && strcmp(target,path)==0)_exit(86);}
int unlink(const char* path){boundary(path);int(*next)(const char*)=dlsym(RTLD_NEXT,"unlink");return next(path);}
int remove(const char* path){boundary(path);int(*next)(const char*)=dlsym(RTLD_NEXT,"remove");return next(path);}
