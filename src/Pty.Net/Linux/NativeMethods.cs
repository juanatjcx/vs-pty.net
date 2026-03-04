// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

namespace Pty.Net.Linux
{
    using System;
    using System.Runtime.InteropServices;

    internal static class NativeMethods
    {
        internal const int STDIN_FILENO = 0;

        internal const uint TIOCSIG = 0x4004_5436;
        internal const ulong TIOCSWINSZ = 0x5414;
        internal const int SIGHUP = 1;

        private const string LibSystem = "libc.so.6";
        private const string LibPtySpawn = "libpty_spawn_linux";

        /// <summary>
        /// Native PTY spawn — performs forkpty + chdir + exec entirely in C.
        /// Avoids running managed code in a forked child process, which segfaults
        /// on .NET because the GC/threadpool threads don't survive fork().
        /// argv and envp must be null-terminated IntPtr arrays (marshalled manually).
        /// </summary>
        [DllImport(LibPtySpawn, SetLastError = true, CharSet = CharSet.Ansi)]
        internal static extern int pty_spawn_linux(
            string exe,
            IntPtr[] argv,
            IntPtr[] envp,
            string cwd,
            ushort rows,
            ushort cols,
            out int out_fd,
            out int out_pid);

        // pid_t waitpid(pid_t, int *, int)
        [DllImport(LibSystem, SetLastError = true)]
        internal static extern int waitpid(int pid, ref int status, int options);

        // int ioctl(int fd, unsigned long request, ...)
        [DllImport(LibSystem, SetLastError = true)]
        internal static extern int ioctl(int fd, ulong request, int data);

        [DllImport(LibSystem, SetLastError = true)]
        internal static extern int ioctl(int fd, ulong request, ref WinSize winSize);

        [DllImport(LibSystem, SetLastError = true)]
        internal static extern int kill(int pid, int signal);

        [StructLayout(LayoutKind.Sequential)]
        public struct WinSize
        {
            public ushort Rows;
            public ushort Cols;
            public ushort XPixel;
            public ushort YPixel;

            public WinSize(ushort rows, ushort cols)
            {
                this.Rows = rows;
                this.Cols = cols;
                this.XPixel = 0;
                this.YPixel = 0;
            }
        }
    }
}
