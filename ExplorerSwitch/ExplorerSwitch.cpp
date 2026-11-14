#include "Registry.h"
#include <iostream>
#include <string>

bool show_status() {
	bool success = true;
	for (const Setting& setting : settings) {
		RegistryValue current;
		RegistryValue original;
		bool backupExists = false;
		if (!read_setting(setting, current) || !read_backup(setting, original, backupExists)) {
			success = false;
			continue;
		}
		std::wcout << L"[+] " << setting.label << L": ";
		if (!current.exists) std::wcout << L"not set (Explorer uses its default)";
		else if (current.value == setting.showValue) std::wcout << L"shown";
		else if (current.value == setting.hideValue) std::wcout << L"hidden";
		else std::wcout << L"other DWORD value: " << current.value;
		std::wcout << (backupExists ? L" | original saved\n" : L" | no backup\n");
	}
	return success;
}

void notify_explorer() {
	DWORD_PTR result = 0;
	LRESULT sent = SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
		reinterpret_cast<LPARAM>(L"Advanced"), SMTO_ABORTIFHUNG, 100, &result);
	if (!sent) std::wcout << L"[!] The notification did not reach all windows.\n";
	std::wcout << L"[!] If Explorer keeps the old view, close and reopen its windows.\n"
		<< L"[!] If necessary, sign out and sign in again.\n";
}

void print_help() {
	std::wcout << L"ExplorerSwitch - File Explorer settings through WinAPI\n\n"
		<< L"  ExplorerSwitch.exe                   Open the menu\n"
		<< L"  ExplorerSwitch.exe status            Read current values\n"
		<< L"  ExplorerSwitch.exe extensions show   Show file extensions\n"
		<< L"  ExplorerSwitch.exe extensions hide   Hide file extensions\n"
		<< L"  ExplorerSwitch.exe hidden show       Show hidden files\n"
		<< L"  ExplorerSwitch.exe hidden hide       Hide hidden files\n"
		<< L"  ExplorerSwitch.exe restore           Restore saved values\n"
		<< L"  ExplorerSwitch.exe help              Show this help\n\n"
		<< L"[!] Changes apply to your Windows account. No administrator access is necessary.\n"
		<< L"[!] Restore uses the first saved value for each setting.\n";
}

int run_command(int argc, wchar_t* argv[]) {
	std::wstring command = argv[1];
	if (argc == 2 && (command == L"help" || command == L"--help" || command == L"/?")) {
		print_help();
		return 0;
	}
	if (argc == 2 && command == L"status") return show_status() ? 0 : 1;
	bool changed = false;
	bool success = false;
	if (argc == 2 && command == L"restore") {
		success = restore_settings(changed);
	}
	else if (argc == 3 && (command == L"extensions" || command == L"hidden") &&
		(std::wstring(argv[2]) == L"show" || std::wstring(argv[2]) == L"hide")) {
		int index = command == L"extensions" ? 0 : 1;
		success = change_setting(settings[index], std::wstring(argv[2]) == L"show", changed);
	}
	else {
		std::wcerr << L"[-] Invalid command. Use 'ExplorerSwitch.exe help'.\n";
		return 2;
	}
	if (changed) notify_explorer();
	return success ? 0 : 1;
}

int run_menu() {
	std::wcout << L"\nExplorerSwitch\n";
	show_status();
	while (true) {
		std::wcout << L"\n1. Show file extensions\n2. Hide file extensions\n"
			<< L"3. Show hidden files\n4. Hide hidden files\n"
			<< L"5. Restore original values\n6. Show status\n0. Exit\n\nChoice: ";
		std::wstring choice;
		if (!std::getline(std::wcin, choice) || choice == L"0") return 0;
		if (choice == L"6") {
			show_status();
			continue;
		}
		bool changed = false;
		if (choice == L"1") change_setting(settings[0], true, changed);
		else if (choice == L"2") change_setting(settings[0], false, changed);
		else if (choice == L"3") change_setting(settings[1], true, changed);
		else if (choice == L"4") change_setting(settings[1], false, changed);
		else if (choice == L"5") restore_settings(changed);
		else std::wcout << L"[-] Enter a number from 0 to 6.\n";
		if (changed) notify_explorer();
	}
}

int wmain(int argc, wchar_t* argv[]) {
	// One instance per Windows session protects the original backup.
	HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Local\\ExplorerSwitch.RegistryLock");
	if (hMutex == NULL) {
		print_error(L"Cannot create the instance lock", static_cast<LSTATUS>(GetLastError()));
		return 1;
	}
	DWORD wait = WaitForSingleObject(hMutex, 0);
	if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
		std::wcerr << L"[-] Cannot acquire the lock. Close other ExplorerSwitch instances, then try again.\n";
		CloseHandle(hMutex);
		return 1;
	}
	int result = argc == 1 ? run_menu() : run_command(argc, argv);
	ReleaseMutex(hMutex);
	CloseHandle(hMutex);
	return result;
}
