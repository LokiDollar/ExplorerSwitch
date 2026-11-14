#include "Registry.h"
#include <iostream>

const wchar_t* const explorerPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";
const wchar_t* const backupPath = L"Software\\ExplorerSwitch\\Backup";

// Hidden uses 1 and 2. HideFileExt uses 0 and 1.
const Setting settings[2] = {
	{ L"HideFileExt", L"File extensions", 0, 1 },
	{ L"Hidden", L"Hidden files", 1, 2 }
};

void print_error(const wchar_t* action, LSTATUS error) {
	wchar_t message[512] = {};
	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, static_cast<DWORD>(error), 0, message, 512, NULL);
	std::wcerr << L"[-] " << action << L". Windows error " << error << L": " << message << L'\n';
}

// A missing key or value is a valid result. Other errors must stop the change.
static LSTATUS read_value(const wchar_t* path, const wchar_t* name, DWORD expectedType,
	void* data, DWORD expectedSize, bool& exists) {
	exists = false;
	HKEY hKey = NULL;
	LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_QUERY_VALUE, &hKey);
	if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
	if (error != ERROR_SUCCESS) return error;

	DWORD type = 0;
	DWORD size = expectedSize;
	error = RegQueryValueExW(hKey, name, NULL, &type, static_cast<BYTE*>(data), &size);
	RegCloseKey(hKey);
	if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
	if (error != ERROR_SUCCESS) return error;
	if (type != expectedType || size != expectedSize) return ERROR_INVALID_DATA;
	exists = true;
	return ERROR_SUCCESS;
}

bool read_setting(const Setting& setting, RegistryValue& result) {
	result = {};
	LSTATUS error = read_value(explorerPath, setting.name, REG_DWORD,
		&result.value, sizeof(result.value), result.exists);
	if (error == ERROR_SUCCESS) return true;
	print_error(setting.name, error);
	return false;
}

bool read_backup(const Setting& setting, RegistryValue& result, bool& exists) {
	result = {};
	// Two DWORDs: [original value existed, original DWORD data].
	DWORD data[2] = {};
	LSTATUS error = read_value(backupPath, setting.name, REG_BINARY, data, sizeof(data), exists);
	if (error == ERROR_SUCCESS && exists && data[0] > 1) error = ERROR_INVALID_DATA;
	if (error != ERROR_SUCCESS) {
		print_error(L"Cannot read the backup", error);
		return false;
	}
	if (exists) {
		result.exists = data[0] == 1;
		result.value = data[1];
	}
	return true;
}

static bool save_backup(const Setting& setting, const RegistryValue& original) {
	RegistryValue saved;
	bool exists = false;
	if (!read_backup(setting, saved, exists)) return false;

	HKEY hKey = NULL;
	LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, backupPath, 0, NULL, 0,
		KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &hKey, NULL);
	if (error != ERROR_SUCCESS) {
		print_error(L"Cannot open the backup key", error);
		return false;
	}

	// Keep the first original value until restore succeeds.
	if (!exists) {
		DWORD data[2] = { original.exists ? 1u : 0u, original.value };
		error = RegSetValueExW(hKey, setting.name, 0, REG_BINARY,
			reinterpret_cast<const BYTE*>(data), sizeof(data));
	}
	// Commit the backup before the first write to Explorer.
	if (error == ERROR_SUCCESS) error = RegFlushKey(hKey);
	RegCloseKey(hKey);
	if (error == ERROR_SUCCESS) return true;
	print_error(L"Cannot save the backup", error);
	return false;
}

static bool write_setting(const Setting& setting, const RegistryValue& target) {
	HKEY hKey = NULL;
	LSTATUS error;
	if (target.exists) {
		error = RegCreateKeyExW(HKEY_CURRENT_USER, explorerPath, 0, NULL, 0,
			KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &hKey, NULL);
	}
	else {
		error = RegOpenKeyExW(HKEY_CURRENT_USER, explorerPath, 0,
			KEY_QUERY_VALUE | KEY_SET_VALUE, &hKey);
		if (error == ERROR_FILE_NOT_FOUND) return true;
	}
	if (error != ERROR_SUCCESS) {
		print_error(L"Cannot open the Explorer key", error);
		return false;
	}

	if (target.exists) {
		error = RegSetValueExW(hKey, setting.name, 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&target.value), sizeof(target.value));
	}
	else {
		error = RegDeleteValueW(hKey, setting.name);
		if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS;
	}
	// Restore must reach disk before we remove its backup.
	if (error == ERROR_SUCCESS) error = RegFlushKey(hKey);
	RegCloseKey(hKey);
	if (error == ERROR_SUCCESS) return true;
	print_error(setting.name, error);
	return false;
}

bool change_setting(const Setting& setting, bool show, bool& changed) {
	changed = false;
	RegistryValue current;
	if (!read_setting(setting, current)) return false;
	DWORD wanted = show ? setting.showValue : setting.hideValue;
	if (current.exists && current.value == wanted) {
		std::wcout << L"[+] " << setting.label << L": already " << (show ? L"shown" : L"hidden") << L".\n";
		return true;
	}
	if (!save_backup(setting, current)) return false;
	RegistryValue target = { true, wanted };
	// A flush can fail after the write. Notify Explorer even in that case.
	changed = true;
	if (!write_setting(setting, target)) return false;
	std::wcout << L"[+] " << setting.label << L": " << (show ? L"shown" : L"hidden") << L".\n";
	return true;
}

bool restore_settings(bool& changed) {
	changed = false;
	bool success = true;
	bool found = false;
	for (const Setting& setting : settings) {
		RegistryValue original;
		bool exists = false;
		if (!read_backup(setting, original, exists)) {
			success = false;
			continue;
		}
		if (!exists) continue;
		found = true;
		changed = true;
		if (!write_setting(setting, original)) {
			success = false;
			continue;
		}

		HKEY hKey = NULL;
		LSTATUS error = RegOpenKeyExW(HKEY_CURRENT_USER, backupPath, 0, KEY_SET_VALUE, &hKey);
		if (error == ERROR_SUCCESS) {
			error = RegDeleteValueW(hKey, setting.name);
			RegCloseKey(hKey);
		}
		if (error != ERROR_SUCCESS) {
			print_error(L"Value restored, but backup removal failed. Run restore again", error);
			success = false;
			continue;
		}
		std::wcout << L"[+] Restored: " << setting.label << L".\n";
	}
	if (success && !found) std::wcout << L"[!] No saved values to restore.\n";
	return success;
}
