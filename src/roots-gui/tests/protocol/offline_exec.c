/* Copyright (c) 2026 Plan-B Foundation
 * Distributed under the MIT software license; see COPYING. */
/* Linux-only test runner: restrict this process and its descendants, never the
 * host. Fail closed when seccomp is unavailable. No capability is added. */
#define _GNU_SOURCE
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>

#if defined(__x86_64__)
#define HOST_ARCH AUDIT_ARCH_X86_64
#elif defined(__aarch64__)
#define HOST_ARCH AUDIT_ARCH_AARCH64
#else
#error Unsupported architecture for the Linux offline test runner
#endif

#define DENY_SYSCALL(n) \
    BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (n), 0, 1), \
    BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM)

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: offline_exec COMMAND [ARG ...]\n"); return 2; }
    struct sock_filter filter[] = {
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, HOST_ARCH, 1, 0),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
        /* Reject x32 ABI syscall-number aliases too. */
        BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS),
        DENY_SYSCALL(SYS_socket),
        DENY_SYSCALL(SYS_connect),
        DENY_SYSCALL(SYS_bind),
        DENY_SYSCALL(SYS_listen),
        DENY_SYSCALL(SYS_accept),
#ifdef SYS_accept4
        DENY_SYSCALL(SYS_accept4),
#endif
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
    };
    const struct sock_fprog program = {sizeof(filter) / sizeof(filter[0]), filter};
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) ||
        prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program)) {
        perror("offline seccomp unavailable"); return 125;
    }
    execvp(argv[1], argv + 1);
    perror("offline command exec");
    return 126;
}
