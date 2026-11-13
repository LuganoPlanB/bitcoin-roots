// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallettool_input.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <string_view>

#ifdef WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace wallet::WalletTool {
namespace {

void InputRequire(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

#ifdef WIN32
volatile LONG g_interrupted{0};
BOOL WINAPI InterruptHandler(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    InterlockedExchange(&g_interrupted, 1);
    return TRUE;
}
#else
volatile std::sig_atomic_t g_interrupted{0};
void InterruptHandler(int) { g_interrupted = 1; }
#endif

using detail::PassphraseEnd;
using detail::PassphraseLine;

#ifndef WIN32
class InputFd
{
    int m_fd;
public:
    explicit InputFd(int fd) : m_fd(fd) {}
    ~InputFd() { if (m_fd != -1) close(m_fd); }
    int Get() const { return m_fd; }
};

class TerminalState
{
    const int m_fd;
    termios m_saved{};
    bool m_changed{false};

    bool RestoreNative() const noexcept
    {
        int result;
        do { result = tcsetattr(m_fd, TCSANOW, &m_saved); } while (result < 0 && errno == EINTR);
        return result == 0;
    }
public:
    explicit TerminalState(int fd) : m_fd(fd)
    {
        InputRequire(tcgetattr(m_fd, &m_saved) == 0, "Cannot read legacy passphrase terminal settings");
        auto mode{m_saved};
        mode.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
        mode.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF | INPCK | IGNPAR);
        mode.c_cflag = (mode.c_cflag & ~(CSIZE | PARENB)) | CS8;
        mode.c_cc[VMIN] = 1;
        mode.c_cc[VTIME] = 0;
        // Retain ISIG so ordinary terminal interruption remains available.
        if (tcsetattr(m_fd, TCSANOW, &mode) != 0) {
            // A constructor that throws will not run its own destructor.
            RestoreNative();
            throw std::runtime_error("Cannot disable legacy passphrase terminal echo");
        }
        m_changed = true;
    }
    ~TerminalState() { if (m_changed) RestoreNative(); }
    void Restore()
    {
        InputRequire(RestoreNative(), "Cannot restore legacy passphrase terminal settings");
        m_changed = false;
    }
};

bool ReadByte(int fd, char* byte, bool wait)
{
    while (true) {
        CheckLegacyWalletToolInterrupted();
        pollfd descriptor{fd, POLLIN, 0};
        const int ready{poll(&descriptor, 1, wait ? 100 : 0)};
        if (ready < 0 && errno == EINTR) continue;
        InputRequire(ready >= 0 && !(descriptor.revents & (POLLERR | POLLNVAL)), "Cannot read legacy passphrase input");
        if (ready == 0) {
            if (wait) continue;
            return false;
        }
        const auto count{read(fd, byte, 1)};
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (wait) continue;
            CheckLegacyWalletToolInterrupted();
            return false;
        }
        InputRequire(count >= 0, "Cannot read legacy passphrase input");
        CheckLegacyWalletToolInterrupted();
        return count == 1;
    }
}

SecureString ReadBytes(int fd, bool trailing)
{
    PassphraseLine line;
    auto byte{make_secure_unique<char>()};
    while (ReadByte(fd, byte.get(), /*wait=*/true)) {
        if (line.Push(*byte)) {
            // No read-ahead buffer exists. A zero-time readiness observation
            // covers bytes supplied now, with no claim about later arrivals.
            if (trailing) InputRequire(!ReadByte(fd, byte.get(), /*wait=*/false), "Trailing legacy passphrase input");
            return line.Finish(PassphraseEnd::BYTE_LINE);
        }
    }
    return line.Finish(PassphraseEnd::END_OF_FILE);
}

void TerminalWrite(int fd, std::string_view bytes)
{
    while (!bytes.empty()) {
        CheckLegacyWalletToolInterrupted();
        const auto count{write(fd, bytes.data(), bytes.size())};
        if (count < 0 && errno == EINTR) continue;
        InputRequire(count > 0, "Cannot write legacy passphrase terminal prompt");
        bytes.remove_prefix(count);
    }
}
#else
class InputHandle
{
    HANDLE m_handle;
public:
    explicit InputHandle(HANDLE handle) : m_handle(handle) {}
    ~InputHandle() { if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle); }
    HANDLE Get() const { return m_handle; }
};

class ConsoleState
{
    HANDLE m_handle;
    DWORD m_saved{0};
    bool m_changed{false};
public:
    explicit ConsoleState(HANDLE handle) : m_handle(handle)
    {
        InputRequire(GetConsoleMode(handle, &m_saved), "Cannot read legacy passphrase console settings");
        InputRequire(SetConsoleMode(handle, (m_saved | ENABLE_PROCESSED_INPUT) & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_VIRTUAL_TERMINAL_INPUT)), "Cannot disable legacy passphrase console echo");
        m_changed = true;
    }
    ~ConsoleState() { if (m_changed) SetConsoleMode(m_handle, m_saved); }
    void Restore()
    {
        InputRequire(SetConsoleMode(m_handle, m_saved), "Cannot restore legacy passphrase console settings");
        m_changed = false;
    }
};

bool ReadByte(HANDLE handle, char* byte, bool wait)
{
    const auto type{GetFileType(handle)};
    InputRequire(type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK, "Legacy stdin passphrase requires a raw byte pipe or file");
    while (true) {
        CheckLegacyWalletToolInterrupted();
        if (type == FILE_TYPE_PIPE) {
            DWORD available{0};
            if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
                if (GetLastError() == ERROR_BROKEN_PIPE) return false;
                throw std::runtime_error("Cannot inspect legacy passphrase pipe");
            }
            if (available == 0) {
                if (!wait) return false;
                Sleep(10);
                continue;
            }
        }
        DWORD count{0};
        const bool ok{ReadFile(handle, byte, 1, &count, nullptr) != 0};
        InputRequire(ok || GetLastError() == ERROR_BROKEN_PIPE, "Cannot read legacy passphrase input");
        CheckLegacyWalletToolInterrupted();
        return ok && count == 1;
    }
}

SecureString ReadBytes(HANDLE handle)
{
    PassphraseLine line;
    auto byte{make_secure_unique<char>()};
    while (ReadByte(handle, byte.get(), /*wait=*/true)) {
        if (line.Push(*byte)) {
            InputRequire(!ReadByte(handle, byte.get(), /*wait=*/false), "Trailing legacy passphrase input");
            return line.Finish(PassphraseEnd::BYTE_LINE);
        }
    }
    return line.Finish(PassphraseEnd::END_OF_FILE);
}

SecureString ReadConsoleBytes(HANDLE handle)
{
    PassphraseLine line;
    auto record{make_secure_unique<INPUT_RECORD>()};
    // Native UTF-16 and UTF-8 conversion scratch is also secure-owned.
    auto wide{make_secure_unique<std::array<wchar_t, 2>>()};
    auto utf8{make_secure_unique<std::array<char, 4>>()};
    bool high{false};
    while (true) {
        CheckLegacyWalletToolInterrupted();
        const auto ready{WaitForSingleObject(handle, 100)};
        InputRequire(ready == WAIT_OBJECT_0 || ready == WAIT_TIMEOUT, "Cannot wait for legacy passphrase console input");
        if (ready == WAIT_TIMEOUT) continue;
        DWORD count{0};
        InputRequire(ReadConsoleInputW(handle, record.get(), 1, &count) && count == 1, "Cannot read legacy passphrase console input");
        if (record->EventType != KEY_EVENT || !record->Event.KeyEvent.bKeyDown) continue;
        const auto& key{record->Event.KeyEvent};
        const auto ch{key.uChar.UnicodeChar};
        const bool nul{ch == 0 && key.wVirtualKeyCode == VK_SPACE && (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))};
        if (ch == 0 && !nul) continue; // Function/modifier keys carry no text.
        for (WORD repeat{0}; repeat < key.wRepeatCount; ++repeat) {
            if (ch >= 0xd800 && ch <= 0xdbff) {
                InputRequire(!high, "Invalid Unicode legacy passphrase input");
                (*wide)[0] = ch;
                high = true;
                continue;
            }
            InputRequire((ch >= 0xdc00 && ch <= 0xdfff) == high, "Invalid Unicode legacy passphrase input");
            (*wide)[high ? 1 : 0] = ch;
            const int bytes{WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide->data(), high ? 2 : 1, utf8->data(), utf8->size(), nullptr, nullptr)};
            InputRequire(bytes > 0, "Invalid Unicode legacy passphrase input");
            high = false;
            for (int i{0}; i < bytes; ++i) {
                // Native Enter produces CR; it is a terminal key event, not
                // byte-stream CRLF. The Unicode conversion never normalizes.
                if (key.wVirtualKeyCode == VK_RETURN && (*utf8)[i] == '\r') return line.Finish(PassphraseEnd::NATIVE_ENTER);
                if (line.Push((*utf8)[i])) return line.Finish(PassphraseEnd::BYTE_LINE);
            }
        }
    }
}
#endif
} // namespace

struct LegacyWalletToolSignals::Impl {
#ifndef WIN32
    static constexpr std::array<int, 5> SIGNALS{SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGPIPE};
    std::array<struct sigaction, SIGNALS.size()> saved{};
    size_t installed{0};
    ~Impl()
    {
        while (installed > 0) {
            --installed;
            sigaction(SIGNALS[installed], &saved[installed], nullptr);
        }
    }
#else
    bool installed{false};
    ~Impl() { if (installed) SetConsoleCtrlHandler(InterruptHandler, FALSE); }
#endif
};

LegacyWalletToolSignals::LegacyWalletToolSignals() : m_impl(std::make_unique<Impl>())
{
#ifndef WIN32
    g_interrupted = 0;
    struct sigaction action{};
    sigemptyset(&action.sa_mask);
    for (const auto signal : Impl::SIGNALS) {
        action.sa_handler = signal == SIGPIPE ? SIG_IGN : InterruptHandler;
        InputRequire(sigaction(signal, &action, &m_impl->saved[m_impl->installed]) == 0, "Cannot install legacy export interrupt handler");
        ++m_impl->installed;
    }
#else
    InterlockedExchange(&g_interrupted, 0);
    InputRequire(SetConsoleCtrlHandler(InterruptHandler, TRUE), "Cannot install legacy export interrupt handler");
    m_impl->installed = true;
#endif
}
LegacyWalletToolSignals::~LegacyWalletToolSignals() = default;

bool LegacyWalletToolInterrupted() noexcept
{
#ifdef WIN32
    return InterlockedCompareExchange(&g_interrupted, 0, 0) != 0;
#else
    return g_interrupted != 0;
#endif
}
void CheckLegacyWalletToolInterrupted()
{
    InputRequire(!LegacyWalletToolInterrupted(), "Legacy export interrupted");
}

SecureString ReadLegacyWalletToolPassphrase(bool use_stdin)
{
    CheckLegacyWalletToolInterrupted();
#ifndef WIN32
    if (use_stdin && !isatty(STDIN_FILENO)) return ReadBytes(STDIN_FILENO, /*trailing=*/true);
    InputFd terminal{use_stdin ? dup(STDIN_FILENO) : open("/dev/tty", O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK)};
    InputRequire(terminal.Get() != -1, "Encrypted legacy wallet requires a controlling terminal or -stdinwalletpassphrase");
    TerminalState settings{terminal.Get()};
    if (!use_stdin) TerminalWrite(terminal.Get(), "Wallet passphrase (finish with Ctrl-J): ");
    auto result{ReadBytes(terminal.Get(), /*trailing=*/use_stdin)};
    if (!use_stdin) TerminalWrite(terminal.Get(), "\n");
    settings.Restore();
#else
    if (use_stdin) return ReadBytes(GetStdHandle(STD_INPUT_HANDLE));
    InputHandle terminal{CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
    InputHandle output{CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
    InputRequire(terminal.Get() != INVALID_HANDLE_VALUE && output.Get() != INVALID_HANDLE_VALUE, "Encrypted legacy wallet requires a controlling terminal or -stdinwalletpassphrase");
    ConsoleState settings{terminal.Get()};
    DWORD written{0};
    constexpr std::wstring_view prompt{L"Wallet passphrase: "};
    InputRequire(WriteConsoleW(output.Get(), prompt.data(), prompt.size(), &written, nullptr) && written == prompt.size(), "Cannot write legacy passphrase console prompt");
    auto result{ReadConsoleBytes(terminal.Get())};
    InputRequire(WriteConsoleW(output.Get(), L"\r\n", 2, &written, nullptr) && written == 2, "Cannot write legacy passphrase console prompt");
    settings.Restore();
#endif
    CheckLegacyWalletToolInterrupted();
    return result;
}
} // namespace wallet::WalletTool
