// Copyright (c) 2025-present The Bitcoin Roots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#ifndef WIN32

#if HAVE_CLOSE_RANGE_LINUX
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <linux/close_range.h>
#endif

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

namespace subprocess::detail {

#ifdef __linux__
// The kernel ABI for getdents64. No libc directory iterator or heap storage is
// used after fork: /proc enumerates the actual descriptors, including ones above
// a subsequently lowered RLIMIT_NOFILE.
struct LinuxDirent64 {
    uint64_t inode;
    int64_t offset;
    unsigned short record_length;
    unsigned char type;
    char name[1];
};

static bool CloseProcDescriptors() noexcept
{
    const int directory = open("/proc/self/fd", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) return false;
    alignas(LinuxDirent64) char buffer[4096];
    bool success{true};
    for (;;) {
        const long count = syscall(SYS_getdents64, directory, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            success = false;
            break;
        }
        for (long position{0}; position < count;) {
            constexpr size_t name_offset{offsetof(LinuxDirent64, name)};
            if (count - position <= static_cast<long>(name_offset)) {
                success = false;
                break;
            }
            unsigned short record_length;
            std::memcpy(&record_length, buffer + position + offsetof(LinuxDirent64, record_length), sizeof(record_length));
            const char* name{buffer + position + name_offset};
            if (record_length <= name_offset || record_length > count - position) {
                success = false;
                break;
            }
            int fd{0};
            size_t digit{0};
            for (; digit < record_length - name_offset; ++digit) {
                const char ch{name[digit]};
                if (ch < '0' || ch > '9' || fd > (INT_MAX - (ch - '0')) / 10) break;
                fd = fd * 10 + ch - '0';
            }
            if (digit > 0 && digit < record_length - name_offset && name[digit] == '\0' && fd > 3 && fd != directory) close(fd);
            position += record_length;
        }
        if (!success) break;
    }
    close(directory);
    return success;
}
#endif

bool subprocess_close_all_fds(int& except_fd, [[maybe_unused]] bool force_fallback) noexcept
{
    if (except_fd < 3) return false;
    // Keep the exec-error pipe at a fixed descriptor so native close-from APIs
    // need no exception list or scan of a potentially enormous numeric range.
    if (except_fd != 3) {
        if (dup2(except_fd, 3) < 0) return false;
        close(except_fd);
        except_fd = 3;
    }
    if (fcntl(3, F_SETFD, FD_CLOEXEC) < 0) return false;
#if HAVE_CLOSE_RANGE_GENERIC || HAVE_CLOSE_RANGE_LINUX
    if (!force_fallback && close_range(4, UINT_MAX, 0) == 0) return true;
#endif
#ifdef __linux__
    return CloseProcDescriptors();
#elif HAVE_CLOSEFROM_RETURN_INT
    return closefrom(4) == 0;
#elif HAVE_CLOSEFROM
    closefrom(4);
    return true;
#else
    // An unsupported or unavailable safe closure mechanism must not allow the
    // signer to execute with unrelated inherited descriptors.
    return false;
#endif
}

} // namespace subprocess::detail

#endif // !WIN32
