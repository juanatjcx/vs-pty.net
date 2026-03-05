/**
 * pty_spawn_linux.c - Native PTY spawn for Linux
 *
 * Performs the entire forkpty → chdir → exec sequence in pure C to avoid
 * running managed code in a forked child process. On .NET, the GC, finalizer,
 * and threadpool threads don't survive fork(), so any managed code in the
 * child (Environment.CurrentDirectory, Dictionary iteration, setenv) will
 * segfault or deadlock.
 *
 * This follows the same pattern as pty_shim.c (macOS ARM64 native shim).
 *
 * All child-side code is async-signal-safe per POSIX.
 * Uses a CLOEXEC pipe for reliable exec error reporting.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

/**
 * Spawn a process inside a new PTY, performing all fork/exec work in native C.
 *
 * @param exe       Path or name of the executable to run
 * @param argv      NULL-terminated argument array (argv[0] should be the program name)
 * @param envp      NULL-terminated environment array ("KEY=VALUE" strings)
 * @param cwd       Working directory for the child process (NULL to inherit)
 * @param rows      Initial terminal rows
 * @param cols      Initial terminal columns
 * @param out_fd    [out] PTY master file descriptor
 * @param out_pid   [out] Child process PID
 * @return          0 on success, -1 on error (check errno)
 */
int pty_spawn_linux(
    const char *exe,
    char *const argv[],
    char *const envp[],
    const char *cwd,
    unsigned short rows,
    unsigned short cols,
    int *out_fd,
    int *out_pid)
{
    if (!exe || !argv || !out_fd || !out_pid) {
        errno = EINVAL;
        return -1;
    }

    /* Set up terminal size */
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = rows;
    ws.ws_col = cols;

    /* Set up terminal attributes (same defaults as the managed code) */
    struct termios term;
    memset(&term, 0, sizeof(term));
    term.c_iflag = ICRNL | IXON | IXANY | IMAXBEL | BRKINT | IUTF8;
    term.c_oflag = OPOST | ONLCR;
    term.c_cflag = CREAD | CS8 | HUPCL;
    term.c_lflag = ICANON | ISIG | IEXTEN | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL;
    cfsetispeed(&term, B38400);
    cfsetospeed(&term, B38400);

    /* Control characters */
    term.c_cc[VINTR]    = 3;      /* Ctrl+C */
    term.c_cc[VQUIT]    = 0x1c;   /* Ctrl+\ */
    term.c_cc[VERASE]   = 0x7f;   /* DEL */
    term.c_cc[VKILL]    = 21;     /* Ctrl+U */
    term.c_cc[VEOF]     = 4;      /* Ctrl+D */
    term.c_cc[VSTART]   = 17;     /* Ctrl+Q */
    term.c_cc[VSTOP]    = 19;     /* Ctrl+S */
    term.c_cc[VSUSP]    = 26;     /* Ctrl+Z */
    term.c_cc[VREPRINT] = 18;     /* Ctrl+R */
    term.c_cc[VWERASE]  = 23;     /* Ctrl+W */
    term.c_cc[VLNEXT]   = 22;     /* Ctrl+V */
#ifdef VDISCARD
    term.c_cc[VDISCARD] = 15;     /* Ctrl+O */
#endif
    term.c_cc[VMIN]     = 1;
    term.c_cc[VTIME]    = 0;
    term.c_cc[VEOL]     = 0;
    term.c_cc[VEOL2]    = 0;

    /*
     * Create a CLOEXEC pipe for exec error reporting.
     * If exec succeeds, the pipe is closed automatically (CLOEXEC).
     * If exec fails, the child writes errno through the pipe.
     */
    int errpipe[2];
    if (pipe2(errpipe, O_CLOEXEC) == -1) {
        return -1;
    }

    int master_fd = -1;
    pid_t pid = forkpty(&master_fd, NULL, &term, &ws);

    if (pid == -1) {
        int saved_errno = errno;
        close(errpipe[0]);
        close(errpipe[1]);
        errno = saved_errno;
        return -1;
    }

    if (pid == 0) {
        /* ---- Child process (async-signal-safe code only) ---- */

        /* Close the read end of the error pipe */
        close(errpipe[0]);

        /* Change working directory */
        if (cwd && cwd[0] != '\0') {
            if (chdir(cwd) == -1) {
                int err = errno;
                (void)write(errpipe[1], &err, sizeof(err));
                _exit(127);
            }
        }

        /* Execute the program */
        if (envp) {
            execve(exe, argv, envp);

            /*
             * If execve failed and exe doesn't contain '/', try PATH search.
             * We do this manually since execvpe is a GNU extension and we
             * can't use it here (it's not async-signal-safe).
             */
            if (errno == ENOENT && strchr(exe, '/') == NULL) {
                const char *path = NULL;
                /* Search envp for PATH */
                for (char *const *e = envp; *e; e++) {
                    if (strncmp(*e, "PATH=", 5) == 0) {
                        path = *e + 5;
                        break;
                    }
                }
                if (!path) {
                    path = "/usr/local/bin:/usr/bin:/bin";
                }

                char fullpath[PATH_MAX];
                const char *start = path;
                while (*start) {
                    const char *end = strchr(start, ':');
                    size_t dirlen = end ? (size_t)(end - start) : strlen(start);

                    if (dirlen > 0 && dirlen + 1 + strlen(exe) < PATH_MAX) {
                        memcpy(fullpath, start, dirlen);
                        fullpath[dirlen] = '/';
                        strcpy(fullpath + dirlen + 1, exe);
                        execve(fullpath, argv, envp);
                    }

                    if (!end) break;
                    start = end + 1;
                }
            }
        } else {
            execvp(exe, argv);
        }

        /* exec failed — report errno through pipe */
        int err = errno;
        (void)write(errpipe[1], &err, sizeof(err));
        _exit(127);
    }

    /* ---- Parent process ---- */

    /* Close the write end of the error pipe */
    close(errpipe[1]);

    /* Check if exec failed by reading from the error pipe */
    int child_errno = 0;
    ssize_t n = read(errpipe[0], &child_errno, sizeof(child_errno));
    close(errpipe[0]);

    if (n > 0) {
        /* exec failed in the child */
        int status;
        waitpid(pid, &status, 0);
        close(master_fd);
        errno = child_errno;
        return -1;
    }

    /* Success */
    *out_fd = master_fd;
    *out_pid = (int)pid;
    return 0;
}
