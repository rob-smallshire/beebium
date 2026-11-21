// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

// The Windows stand-in for a beebium-<model> symlink (#298).
//
// On Linux and macOS each beebium-<model> name is a symlink to the multi-call
// beebium-server, which dispatches on the name it was invoked by. Windows has
// no symlink an ordinary user can rely on, so each name is this small console
// program instead, built once per model with BEEBIUM_LAUNCHER_MODEL set.
//
// It runs the beebium-server.exe beside it as the application, with its own
// command line passed through unchanged. argv[0] therefore still names this
// launcher (beebium-<model>.exe), so the server dispatches to that model and
// every argument reaches it byte for byte, with no re-quoting. The child shares
// this console and inherits its standard handles (the startup information is
// passed through), so Ctrl+C reaches the server directly. The child runs in a
// job object that kills it when this launcher's handle to the job closes, so
// ending the launcher (Task Manager, a parent's TerminateProcess) ends the
// server too. The launcher exits with the server's exit code.
//
// A launcher that cannot start the server says why on stderr in one sentence
// and exits with LAUNCHER_FAILURE_EXIT_CODE.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef BEEBIUM_LAUNCHER_MODEL
#error "BEEBIUM_LAUNCHER_MODEL must name the model, e.g. -DBEEBIUM_LAUNCHER_MODEL=model-b"
#endif

#define BEEBIUM_WIDEN2(x) L##x
#define BEEBIUM_WIDEN(x) BEEBIUM_WIDEN2(x)
#define BEEBIUM_STRINGIFY2(x) #x
#define BEEBIUM_STRINGIFY(x) BEEBIUM_STRINGIFY2(x)
#define LAUNCHER_NAME BEEBIUM_WIDEN("beebium-" BEEBIUM_STRINGIFY(BEEBIUM_LAUNCHER_MODEL))

// EX_SOFTWARE from sysexits.h: the launcher itself failed, not the server.
#define LAUNCHER_FAILURE_EXIT_CODE 70

// The longest path Windows accepts with the \\?\ prefix, in characters.
#define MAX_LONG_PATH 32768

static int fail(const wchar_t* what, DWORD error)
{
    fwprintf(stderr, L"%ls: %ls (Windows error %lu).\n", LAUNCHER_NAME, what, (unsigned long)error);
    return LAUNCHER_FAILURE_EXIT_CODE;
}

// Ctrl+C and Ctrl+Break go to every process on the console, the server
// included; the server decides what they mean. The launcher ignores them and
// keeps waiting, so it reports the server's own exit code. A handler, unlike
// SetConsoleCtrlHandler(NULL, TRUE), is not inherited by the child.
static BOOL WINAPI ignore_console_interrupts(DWORD ctrl_type)
{
    return ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT;
}

int wmain(void)
{
    static wchar_t server_filepath[MAX_LONG_PATH];
    DWORD length = GetModuleFileNameW(NULL, server_filepath, MAX_LONG_PATH);
    if (length == 0 || length >= MAX_LONG_PATH) {
        return fail(L"could not find its own path", GetLastError());
    }
    wchar_t* name = server_filepath;
    for (wchar_t* p = server_filepath; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') {
            name = p + 1;
        }
    }
    const wchar_t server_name[] = L"beebium-server.exe";
    if ((size_t)(name - server_filepath) + (sizeof server_name / sizeof server_name[0]) > MAX_LONG_PATH) {
        return fail(L"its directory path is too long to hold beebium-server.exe", ERROR_FILENAME_EXCED_RANGE);
    }
    memcpy(name, server_name, sizeof server_name);

    // CreateProcessW may write to the command line it is given, so pass a copy.
    const wchar_t* own_command_line = GetCommandLineW();
    size_t command_line_length = wcslen(own_command_line) + 1;
    wchar_t* command_line = HeapAlloc(GetProcessHeap(), 0, command_line_length * sizeof(wchar_t));
    if (command_line == NULL) {
        return fail(L"could not copy its command line", ERROR_NOT_ENOUGH_MEMORY);
    }
    memcpy(command_line, own_command_line, command_line_length * sizeof(wchar_t));

    HANDLE job = CreateJobObjectW(NULL, NULL);
    if (job == NULL) {
        return fail(L"could not create a job object for beebium-server", GetLastError());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits;
    ZeroMemory(&limits, sizeof limits);
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof limits)) {
        return fail(L"could not set the job object to end beebium-server with the launcher", GetLastError());
    }

    STARTUPINFOW startup;
    GetStartupInfoW(&startup);
    PROCESS_INFORMATION process;
    ZeroMemory(&process, sizeof process);
    // Suspended until it is in the job, so nothing it starts escapes the job.
    if (!CreateProcessW(server_filepath, command_line, NULL, NULL, TRUE, CREATE_SUSPENDED, NULL, NULL,
                        &startup, &process)) {
        DWORD error = GetLastError();
        fwprintf(stderr, L"%ls: could not start %ls (Windows error %lu).\n", LAUNCHER_NAME, server_filepath,
                 (unsigned long)error);
        return LAUNCHER_FAILURE_EXIT_CODE;
    }
    if (!AssignProcessToJobObject(job, process.hProcess)) {
        DWORD error = GetLastError();
        TerminateProcess(process.hProcess, LAUNCHER_FAILURE_EXIT_CODE);
        return fail(L"could not put beebium-server in its job object", error);
    }
    SetConsoleCtrlHandler(ignore_console_interrupts, TRUE);
    if (ResumeThread(process.hThread) == (DWORD)-1) {
        DWORD error = GetLastError();
        TerminateProcess(process.hProcess, LAUNCHER_FAILURE_EXIT_CODE);
        return fail(L"could not resume beebium-server", error);
    }
    CloseHandle(process.hThread);

    if (WaitForSingleObject(process.hProcess, INFINITE) != WAIT_OBJECT_0) {
        return fail(L"could not wait for beebium-server", GetLastError());
    }
    DWORD exit_code = LAUNCHER_FAILURE_EXIT_CODE;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        return fail(L"could not read beebium-server's exit code", GetLastError());
    }
    return (int)exit_code;
}
