#include "Registry.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sddl.h>

static int checks = 0;

static void check(bool condition, const char* message) {
	if (!condition) throw std::runtime_error(message);
	checks++;
}

static void put_value(const wchar_t* path, const wchar_t* name, DWORD type,
	const void* data, DWORD size) {
	HKEY hKey = NULL;
	check(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, NULL, 0,
		KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS, "Create fixture key");
	LSTATUS error = RegSetValueExW(hKey, name, 0, type, static_cast<const BYTE*>(data), size);
	RegCloseKey(hKey);
	check(error == ERROR_SUCCESS, "Write fixture value");
}

static void put_dword(const wchar_t* name, DWORD value) {
	put_value(explorerPath, name, REG_DWORD, &value, sizeof(value));
}

static void reset_fixture() {
	// HKCU points to the temporary test key in this process.
	LSTATUS error = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software");
	check(error == ERROR_SUCCESS || error == ERROR_FILE_NOT_FOUND, "Reset fixture");
}

static RegistryValue get_setting(int index) {
	RegistryValue value;
	check(read_setting(settings[index], value), "Read setting");
	return value;
}

// Keep the original ACL so a failed check cannot leave the test key locked.
class WriteBlock {
	HKEY hKey = NULL;
	std::vector<BYTE> original;

public:
	explicit WriteBlock(const wchar_t* path) {
		check(RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, READ_CONTROL | WRITE_DAC, &hKey)
			== ERROR_SUCCESS, "Open fixture ACL");
	}

	void apply() {
		DWORD size = 0;
		check(RegGetKeySecurity(hKey, DACL_SECURITY_INFORMATION, NULL, &size)
			== ERROR_INSUFFICIENT_BUFFER, "Get original ACL size");
		original.resize(size);
		check(RegGetKeySecurity(hKey, DACL_SECURITY_INFORMATION, original.data(), &size)
			== ERROR_SUCCESS, "Read original ACL");
		PSECURITY_DESCRIPTOR denied = NULL;
		check(ConvertStringSecurityDescriptorToSecurityDescriptorW(
			L"D:P(D;;0x2;;;WD)(A;;KA;;;WD)", SDDL_REVISION_1, &denied, NULL) != FALSE,
			"Create a fixture ACL that denies KEY_SET_VALUE");
		LSTATUS error = RegSetKeySecurity(hKey, DACL_SECURITY_INFORMATION, denied);
		LocalFree(denied);
		check(error == ERROR_SUCCESS, "Apply fixture ACL");
	}

	~WriteBlock() {
		if (!original.empty()) RegSetKeySecurity(hKey, DACL_SECURITY_INFORMATION, original.data());
		RegCloseKey(hKey);
	}

	WriteBlock(const WriteBlock&) = delete;
	WriteBlock& operator=(const WriteBlock&) = delete;
};

static void run_checks() {
	bool changed = false;
	RegistryValue original;
	bool exists = false;

	reset_fixture();
	check(!get_setting(0).exists, "Missing key is not an error");
	check(restore_settings(changed) && !changed, "Restore without a backup is a no-op");
	HKEY absent = NULL;
	check(RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &absent)
		== ERROR_FILE_NOT_FOUND, "Read and empty restore do not create keys");

	check(change_setting(settings[0], true, changed) && changed, "Show extensions");
	check(get_setting(0).exists && get_setting(0).value == 0, "Extension value is zero");
	check(read_backup(settings[0], original, exists) && exists && !original.exists,
		"Backup records an absent original");
	check(change_setting(settings[0], false, changed) && changed, "Hide extensions");
	check(read_backup(settings[0], original, exists) && exists && !original.exists,
		"Second change keeps the first original");
	check(restore_settings(changed) && changed, "Restore an absent original");
	check(!get_setting(0).exists, "Restore deletes a value that was absent");
	check(read_backup(settings[0], original, exists) && !exists, "Restore removes the consumed backup");
	check(restore_settings(changed) && !changed, "Repeated restore is a no-op");

	reset_fixture();
	put_dword(L"HideFileExt", 1);
	put_dword(L"Hidden", 2);
	put_dword(L"Unrelated", 123);
	check(change_setting(settings[0], false, changed) && !changed, "No write for the same value");
	check(read_backup(settings[0], original, exists) && !exists, "No backup for a no-op");
	check(change_setting(settings[0], true, changed) && changed, "Change an existing value");
	check(change_setting(settings[0], false, changed) && changed, "Change it again");
	check(read_backup(settings[0], original, exists) && exists && original.exists && original.value == 1,
		"Backup retains the existing original");
	check(change_setting(settings[1], true, changed) && changed, "Show hidden files");
	check(get_setting(1).value == 1, "Hidden uses value one for show");
	check(restore_settings(changed) && changed, "Restore both settings");
	check(get_setting(0).value == 1 && get_setting(1).value == 2, "Both originals return");
	RegistryValue unrelated;
	Setting other = { L"Unrelated", L"Unrelated", 0, 1 };
	check(read_setting(other, unrelated) && unrelated.value == 123, "Unrelated values stay intact");
	check(change_setting(settings[1], true, changed), "Start a new backup cycle");
	check(change_setting(settings[1], false, changed) && changed, "Hide hidden files");
	check(get_setting(1).value == 2, "Hidden uses value two for hide");
	check(restore_settings(changed), "Restore the new backup cycle");

	reset_fixture();
	put_dword(L"Hidden", 42);
	check(change_setting(settings[1], true, changed), "Accept an unusual original DWORD");
	check(restore_settings(changed), "Restore an unusual original DWORD");
	check(get_setting(1).value == 42, "Restore preserves the exact original DWORD");

	reset_fixture();
	wchar_t text[] = L"x";
	put_value(explorerPath, L"HideFileExt", REG_SZ, text, sizeof(text));
	check(!change_setting(settings[0], true, changed) && !changed, "Reject a string instead of DWORD");
	check(read_backup(settings[0], original, exists) && !exists, "Bad current data creates no backup");
	BYTE shortData = 1;
	put_value(explorerPath, L"HideFileExt", REG_DWORD, &shortData, sizeof(shortData));
	check(!change_setting(settings[0], true, changed), "Reject a short DWORD");
	DWORD longData[2] = { 1, 2 };
	put_value(explorerPath, L"HideFileExt", REG_DWORD, longData, sizeof(longData));
	check(!change_setting(settings[0], true, changed), "Reject an oversized DWORD");

	reset_fixture();
	put_dword(L"HideFileExt", 1);
	DWORD badBackup[2] = { 2, 0 };
	put_value(backupPath, L"HideFileExt", REG_BINARY, badBackup, sizeof(badBackup));
	check(!change_setting(settings[0], true, changed) && !changed, "Reject an invalid backup flag");
	check(get_setting(0).value == 1, "Invalid backup prevents a write");
	check(!restore_settings(changed) && !changed, "Invalid backup prevents restore");
	check(get_setting(0).value == 1, "Invalid restore preserves the current value");
	put_value(backupPath, L"HideFileExt", REG_BINARY, &shortData, sizeof(shortData));
	check(!change_setting(settings[0], true, changed), "Reject a short backup");
	put_value(backupPath, L"HideFileExt", REG_DWORD, longData, sizeof(longData));
	check(!change_setting(settings[0], true, changed), "Reject the wrong backup type");

	reset_fixture();
	check(change_setting(settings[0], true, changed), "Create the first backup for partial restore");
	check(change_setting(settings[1], true, changed), "Create the second backup for partial restore");
	put_value(backupPath, L"HideFileExt", REG_BINARY, badBackup, sizeof(badBackup));
	check(!restore_settings(changed) && changed, "Partial restore reports failure");
	check(!get_setting(1).exists, "Partial restore still restores the valid setting");
	check(!read_backup(settings[0], original, exists), "Partial restore retains the invalid backup");
	check(read_backup(settings[1], original, exists) && !exists, "Partial restore removes only the valid backup");

	reset_fixture();
	check(change_setting(settings[1], true, changed), "Back up an absent value");
	check(RegDeleteTreeW(HKEY_CURRENT_USER, explorerPath) == ERROR_SUCCESS, "Remove target key externally");
	check(restore_settings(changed), "Restore tolerates an already absent target key");
	check(!get_setting(1).exists, "Absent target remains absent");

	reset_fixture();
	put_dword(L"Hidden", 2);
	check(change_setting(settings[1], true, changed), "Back up an existing value");
	check(RegDeleteTreeW(HKEY_CURRENT_USER, explorerPath) == ERROR_SUCCESS, "Remove target key again");
	check(restore_settings(changed), "Restore recreates a missing target key");
	check(get_setting(1).value == 2, "Recreated key contains the original value");

	reset_fixture();
	put_dword(L"HideFileExt", 1);
	DWORD placeholder = 0;
	put_value(backupPath, L"Fixture", REG_DWORD, &placeholder, sizeof(placeholder));
	{
		WriteBlock block(backupPath);
		block.apply();
		check(!change_setting(settings[0], true, changed) && !changed, "Backup failure prevents a target write");
		check(get_setting(0).value == 1, "Target stays intact when backup access fails");
	}
	check(change_setting(settings[0], true, changed), "Change after backup access returns");
	{
		WriteBlock block(explorerPath);
		block.apply();
		check(!restore_settings(changed), "Restore reports target access denial");
		check(read_backup(settings[0], original, exists) && exists && original.value == 1,
			"Failed restore retains the original backup");
		check(get_setting(0).value == 0, "Failed restore preserves the current value");
	}
	{
		WriteBlock block(backupPath);
		block.apply();
		check(!restore_settings(changed), "Backup removal failure reports an error");
		check(get_setting(0).value == 1, "Target restores even when backup removal fails");
		check(read_backup(settings[0], original, exists) && exists,
			"Backup remains available after removal failure");
	}
	check(restore_settings(changed), "Restore succeeds after access returns");
	check(read_backup(settings[0], original, exists) && !exists, "Retry consumes the backup");
}

int wmain() {
	std::wstring testPath = L"Software\\ExplorerSwitch.Tests." + std::to_wstring(GetCurrentProcessId());
	HKEY hTest = NULL;
	DWORD disposition = 0;
	LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, testPath.c_str(), 0, NULL, 0,
		KEY_ALL_ACCESS, NULL, &hTest, &disposition);
	if (error != ERROR_SUCCESS) return 1;
	if (disposition != REG_CREATED_NEW_KEY) {
		RegCloseKey(hTest);
		std::wcerr << L"[-] Test key already exists. No test ran.\n";
		return 1;
	}

	// Redirect HKCU only inside this test process. Explorer keeps its real HKCU.
	error = RegOverridePredefKey(HKEY_CURRENT_USER, hTest);
	int result = error == ERROR_SUCCESS ? 0 : 1;
	if (error == ERROR_SUCCESS) {
		try {
			run_checks();
			std::wcout << L"[+] All " << checks << L" checks passed.\n";
		}
		catch (const std::exception& exception) {
			std::cerr << "[-] " << exception.what() << '\n';
			result = 1;
		}
		if (RegOverridePredefKey(HKEY_CURRENT_USER, NULL) != ERROR_SUCCESS) {
			RegCloseKey(hTest);
			return 1;
		}
	}
	if (RegDeleteTreeW(hTest, NULL) != ERROR_SUCCESS) result = 1;
	RegCloseKey(hTest);
	if (RegDeleteKeyW(HKEY_CURRENT_USER, testPath.c_str()) != ERROR_SUCCESS) result = 1;
	return result;
}
