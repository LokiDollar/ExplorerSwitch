#pragma once
#include <Windows.h>

struct Setting {
	const wchar_t* name;
	const wchar_t* label;
	DWORD showValue;
	DWORD hideValue;
};

struct RegistryValue {
	bool exists = false;
	DWORD value = 0;
};

extern const wchar_t* const explorerPath;
extern const wchar_t* const backupPath;
extern const Setting settings[2];

void print_error(const wchar_t* action, LSTATUS error);
bool read_setting(const Setting& setting, RegistryValue& result);
bool read_backup(const Setting& setting, RegistryValue& result, bool& exists);
bool change_setting(const Setting& setting, bool show, bool& changed);
bool restore_settings(bool& changed);
