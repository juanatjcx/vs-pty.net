// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

namespace Pty.Net.Linux
{
    using System;
    using System.Collections.Generic;
    using System.Diagnostics;
    using System.Linq;
    using System.Runtime.InteropServices;
    using System.Threading;
    using System.Threading.Tasks;
    using static Pty.Net.Linux.NativeMethods;

    /// <summary>
    /// Provides a pty connection for linux machines.
    /// </summary>
    internal class PtyProvider : Unix.PtyProvider
    {
        /// <inheritdoc/>
        public override Task<IPtyConnection> StartTerminalAsync(PtyOptions options, TraceSource trace, CancellationToken cancellationToken)
        {
            // Build argv: [exe, args...]
            var argsList = new List<string> { options.App };
            if (options.CommandLine != null)
            {
                argsList.AddRange(options.CommandLine.Where(a => a != null));
            }

            // Build envp: ["KEY=VALUE", ...]
            string[] envpManaged = null;
            if (options.Environment != null && options.Environment.Count > 0)
            {
                envpManaged = options.Environment
                    .Select(kvp => $"{kvp.Key}={kvp.Value}")
                    .ToArray();
            }

            // Marshal to null-terminated IntPtr arrays for C interop
            IntPtr[] argvPtrs = MarshalStringArrayToNullTerminated(argsList.ToArray());
            IntPtr[] envpPtrs = envpManaged != null
                ? MarshalStringArrayToNullTerminated(envpManaged)
                : null;

            try
            {
                int fd;
                int pid;
                int result = pty_spawn_linux(
                    options.App,
                    argvPtrs,
                    envpPtrs,
                    options.Cwd,
                    (ushort)options.Rows,
                    (ushort)options.Cols,
                    out fd,
                    out pid);

                if (result != 0)
                {
                    int errorCode = Marshal.GetLastWin32Error();
                    throw new InvalidOperationException(
                        $"pty_spawn_linux failed for '{options.App}' with error {errorCode}");
                }

                return Task.FromResult<IPtyConnection>(new PtyConnection(fd, pid));
            }
            finally
            {
                FreeStringArray(argvPtrs);
                if (envpPtrs != null) FreeStringArray(envpPtrs);
            }
        }

        /// <summary>
        /// Marshal a string array to a null-terminated IntPtr array suitable for C interop.
        /// Each string is marshalled as a UTF-8 ANSI string via Marshal.StringToHGlobalAnsi.
        /// The array ends with IntPtr.Zero (the null terminator C expects).
        /// </summary>
        private static IntPtr[] MarshalStringArrayToNullTerminated(string[] strings)
        {
            var ptrs = new IntPtr[strings.Length + 1]; // +1 for null terminator
            for (int i = 0; i < strings.Length; i++)
            {
                ptrs[i] = Marshal.StringToHGlobalAnsi(strings[i]);
            }
            ptrs[strings.Length] = IntPtr.Zero;
            return ptrs;
        }

        private static void FreeStringArray(IntPtr[] ptrs)
        {
            for (int i = 0; i < ptrs.Length; i++)
            {
                if (ptrs[i] != IntPtr.Zero)
                {
                    Marshal.FreeHGlobal(ptrs[i]);
                    ptrs[i] = IntPtr.Zero;
                }
            }
        }
    }
}
