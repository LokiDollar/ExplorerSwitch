# ExplorerSwitch

A small C++ console tool that changes two File Explorer preferences through WinAPI.
You can show file extensions, show hidden files, and restore your original registry values.
The tool applies changes to your Windows account. It does not need administrator access.

## Why this project exists

File extensions help you identify a file before you open it.
Hidden files can help you find configuration files and application data.
ExplorerSwitch gives you a menu and commands for these tasks.

The project also gives beginners a small example of Windows Registry access in C++.
The code uses small functions, plain structs, direct WinAPI calls, and short comments.
Console messages use `[+]` for success, `[-]` for errors, and `[!]` for information.

## Features

- Show or hide file extensions.
- Show or hide hidden files.
- Read the current registry values without a change.
- Save each original value before the first change.
- Keep the original backup across later changes and program restarts.
- Restore the exact original DWORD, or remove a value that was originally absent.
- Keep the backup if restore fails.
- Reject unexpected registry types, incorrect data lengths, and invalid backups.
- Prevent a second instance in the same Windows session.

The hidden-files option does not expose protected operating system files.
ExplorerSwitch does not change `ShowSuperHidden`.

## Build in Visual Studio

Requirements:

- Windows with x64 support.
- Visual Studio 2022 or later.
- The **Desktop development with C++** workload and a Windows SDK.

1. Open `ExplorerSwitch.sln`.
2. Select **Release** and **x64**.
3. Select **Build > Build Solution**.
4. Open `bin\x64\Release\ExplorerSwitch.exe`.

The projects select the default C++ toolset from your Visual Studio installation.
The code uses C++17 and the Windows libraries `Advapi32.lib` and `User32.lib`.
The Release build uses the static C++ runtime.

You can also use a Developer PowerShell window for Visual Studio:

```powershell
msbuild ExplorerSwitch.sln /m /p:Configuration=Release /p:Platform=x64
.\bin\x64\Release\ExplorerSwitch.exe
```

## Use the menu

Start the program without arguments:

```text
ExplorerSwitch
[+] File extensions: hidden | no backup
[+] Hidden files: hidden | no backup

1. Show file extensions
2. Hide file extensions
3. Show hidden files
4. Hide hidden files
5. Restore original values
6. Show status
0. Exit

Choice:
```

Enter the number for the action. Select `5` to restore all saved original values.
The status output shows registry data. It does not inspect the current Explorer view.

## Use commands

Run these commands from the folder that contains the executable:

| Command | Action |
| --- | --- |
| `.\ExplorerSwitch.exe` | Open the menu |
| `.\ExplorerSwitch.exe status` | Read both preferences and backup status |
| `.\ExplorerSwitch.exe extensions show` | Show file extensions |
| `.\ExplorerSwitch.exe extensions hide` | Hide file extensions |
| `.\ExplorerSwitch.exe hidden show` | Show hidden files |
| `.\ExplorerSwitch.exe hidden hide` | Hide hidden files |
| `.\ExplorerSwitch.exe restore` | Restore all saved original values |
| `.\ExplorerSwitch.exe help` | Show command help |

Commands use lowercase letters. An invalid command makes no registry changes.
Command exit codes are `0` for success, `1` for an operation error, and `2` for invalid arguments.

Example:

```powershell
.\ExplorerSwitch.exe status
.\ExplorerSwitch.exe extensions show
.\ExplorerSwitch.exe hidden show
.\ExplorerSwitch.exe restore
```

## Registry values

ExplorerSwitch reads and writes these values under `HKEY_CURRENT_USER`:

```text
Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced
```

| Value | Registry type | Show | Hide |
| --- | --- | --- | --- |
| `HideFileExt` | `REG_DWORD` | `0` | `1` |
| `Hidden` | `REG_DWORD` | `1` | `2` |

The two preferences use different numbers. The `Setting` array stores each pair explicitly.
An absent value appears as **not set** in the status output.
Explorer then uses its default behavior.

These values are implementation details of Explorer. Windows versions and organization policies can affect their behavior.

## Backup and restore

Backups use this key under `HKEY_CURRENT_USER`:

```text
Software\ExplorerSwitch\Backup
```

Each backup uses the same value name as its Explorer preference.
Its `REG_BINARY` data contains two DWORDs:

| Position | Meaning |
| --- | --- |
| First DWORD | `1` if the original value existed, otherwise `0` |
| Second DWORD | Original DWORD data, or `0` for an absent original |

A change follows this sequence:

1. Read the current value and validate its type and size.
2. If the requested value matches, return without a write.
3. Save the original value if no backup exists.
4. Flush the backup to disk.
5. Write the requested Explorer value.
6. Flush the Explorer value to disk.
7. Send `WM_SETTINGCHANGE` to top-level windows.

Later changes keep the first backup. A new program process reads the same backup from the registry.
For example, hide, show, and hide still leave the first original value available for restore.

Restore writes the original DWORD or deletes the value if it was originally absent.
It removes that backup only after the target operation and flush succeed.
A failed restore keeps its backup for another attempt.
Restore can succeed for one preference and fail for the other. The command returns an error in this case.

Restore replaces later manual changes to the same preference with the saved original value.
It restores values, not entire keys. Empty registry keys can remain after restore.
This backup covers only the two preferences. It is not a complete registry backup.

`RegFlushKey` writes registry data to disk and can briefly delay other registry operations.
ExplorerSwitch uses it to preserve the order between the backup, the change, and backup removal.

## Code guide

| File | Purpose |
| --- | --- |
| `ExplorerSwitch/ExplorerSwitch.cpp` | Menu, argument checks, status, instance lock, and Explorer notification |
| `ExplorerSwitch/Registry.h` | Two simple structs and function declarations |
| `ExplorerSwitch/Registry.cpp` | Registry access, backup validation, changes, and restore |
| `tests/RegistryTests.cpp` | Checks against a separate registry key |
| `Directory.Build.props` | Common compiler options and output paths |

`RegistryValue` holds a DWORD and a flag that records whether the value exists.
The flag matters because an absent value and a DWORD of zero are different registry states.

Every registry operation checks the WinAPI return code.
Registry functions return their error directly. They do not require `GetLastError()` for that result.
The code closes each registry handle with `RegCloseKey`.
The instance lock uses `CloseHandle` because it is a kernel handle.

The menu and commands call the same registry functions.
The registry functions do not send Explorer notifications. The console layer sends the notification after an attempted change.

## Checks

Build the solution, then run:

```powershell
.\bin\x64\Release\RegistryTests.exe
```

The checks create a unique key at `HKCU\Software\ExplorerSwitch.Tests.<process-id>`.
`RegOverridePredefKey` redirects `HKEY_CURRENT_USER` to this key inside the test process only.
The checks do not modify your Explorer preferences or send Explorer notifications.
The test process removes its temporary key before exit.

Coverage includes absent values, repeated changes, no-op changes, exact restore, unrelated values, and unexpected DWORD data.
It also includes invalid types, invalid lengths, corrupt backups, partial restore, and access-denied errors.
Some checks deliberately cause errors. The final line reports the result.

Both configurations treat compiler warnings as errors at warning level 4.
The GitHub Actions workflow builds and checks Debug and Release on a Windows runner.
Each successful Release job also provides the executable as the `ExplorerSwitch-x64` artifact.

## Limits and error recovery

- Explorer can keep an old view after a registry change. Close and reopen its windows if necessary.
- If the view still does not change, sign out and sign in again.
- The program does not restart Explorer or close your folders.
- The instance lock covers one Windows session. Use only one session for changes to the same account.
- Other applications can change these preferences. The lock does not control those applications.
- Organization policies can block registry writes or replace the values later.
- If a backup is invalid, the program stops that operation and keeps the backup.
- After an access error, correct the access problem and run `restore` again.
- The menu remains open after an operation error. Commands return a nonzero exit code for scripts.

Automated checks validate registry behavior. They do not prove that every Explorer version refreshes its visible folders immediately.

## WinAPI references

- [Registry functions](https://learn.microsoft.com/en-us/windows/win32/sysinfo/registry-functions)
- [RegQueryValueExW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regqueryvalueexw)
- [RegFlushKey](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regflushkey)
- [WM_SETTINGCHANGE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-settingchange)
- [RegOverridePredefKey](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regoverridepredefkey)
