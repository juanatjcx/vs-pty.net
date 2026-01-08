/**
 * pty_shim.c - Native shim for PTY ioctl calls on macOS ARM64
 *
 * This shim works around a .NET P/Invoke limitation where variadic functions
 * (like ioctl) cannot be called correctly on Apple Silicon (ARM64).
 *
 * See: https://github.com/dotnet/runtime/issues/48752
 *
 * The ARM64 ABI on macOS passes variadic arguments on the stack, unlike x86-64
 * which uses registers. This breaks P/Invoke for variadic functions.
 *
 * Solution: Wrap ioctl calls in non-variadic C functions that .NET can call safely.
 */

#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>

/* macOS ioctl constants */
#define PTY_TIOCSWINSZ 0x80087467  /* Set window size */
#define PTY_TIOCGWINSZ 0x40087468  /* Get window size */
#define PTY_TIOCSIG    0x2000745F  /* Send signal to PTY */

/**
 * Window size structure matching POSIX winsize
 */
struct pty_winsize {
    unsigned short ws_row;    /* rows, in characters */
    unsigned short ws_col;    /* columns, in characters */
    unsigned short ws_xpixel; /* horizontal size, pixels (unused) */
    unsigned short ws_ypixel; /* vertical size, pixels (unused) */
};

/**
 * Set PTY window size (wrapper for ioctl TIOCSWINSZ)
 *
 * @param fd    File descriptor of the PTY master
 * @param rows  Number of rows
 * @param cols  Number of columns
 * @return      0 on success, -1 on error (check errno)
 */
int pty_set_window_size(int fd, unsigned short rows, unsigned short cols) {
    struct pty_winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;

    return ioctl(fd, TIOCSWINSZ, &ws);
}

/**
 * Get PTY window size (wrapper for ioctl TIOCGWINSZ)
 *
 * @param fd    File descriptor of the PTY master
 * @param rows  Pointer to store number of rows
 * @param cols  Pointer to store number of columns
 * @return      0 on success, -1 on error (check errno)
 */
int pty_get_window_size(int fd, unsigned short *rows, unsigned short *cols) {
    struct pty_winsize ws;

    int result = ioctl(fd, TIOCGWINSZ, &ws);
    if (result == 0) {
        if (rows) *rows = ws.ws_row;
        if (cols) *cols = ws.ws_col;
    }
    return result;
}

/**
 * Send signal to PTY foreground process group (wrapper for ioctl TIOCSIG)
 *
 * @param fd     File descriptor of the PTY master
 * @param signal Signal number to send (e.g., SIGHUP)
 * @return       0 on success, -1 on error (check errno)
 */
int pty_send_signal(int fd, int signal) {
    return ioctl(fd, TIOCSIG, signal);
}

/**
 * Get the last error number
 * Useful for P/Invoke since Marshal.GetLastWin32Error may not work correctly
 * after calling through the shim.
 *
 * @return Current errno value
 */
int pty_get_errno(void) {
    return errno;
}
