#pragma once

constexpr auto CONFIG_NAME = L"AudioPlaybackConnector.json";
constexpr auto BUFFER_SIZE = 4096;
constexpr auto AUTORUN_KEY_PATH = LR"(Software\Microsoft\Windows\CurrentVersion\Run)";
constexpr auto AUTORUN_VALUE_NAME = L"AudioPlaybackConnector";

std::wstring GetModuleCommandLine(HMODULE hModule)
{
	return L"\"" + GetModuleFsPath(hModule).wstring() + L"\"";
}

bool LoadAutoStartSetting()
{
	std::wstring value(512, L'\0');
	DWORD valueSize = static_cast<DWORD>(value.size() * sizeof(wchar_t));
	auto status = RegGetValueW(HKEY_CURRENT_USER, AUTORUN_KEY_PATH, AUTORUN_VALUE_NAME, RRF_RT_REG_SZ, nullptr, value.data(), &valueSize);
	if (status == ERROR_FILE_NOT_FOUND)
	{
		return false;
	}

	THROW_IF_WIN32_ERROR(status);

	if (valueSize < sizeof(wchar_t))
	{
		return false;
	}

	value.resize(valueSize / sizeof(wchar_t) - 1);
	return value == GetModuleCommandLine(g_hInst);
}

void SaveAutoStartSetting()
{
	wil::unique_hkey hKey;
	THROW_IF_WIN32_ERROR(RegCreateKeyExW(HKEY_CURRENT_USER, AUTORUN_KEY_PATH, 0, nullptr, 0, KEY_SET_VALUE, nullptr, hKey.put(), nullptr));

	if (g_autoStart)
	{
		auto command = GetModuleCommandLine(g_hInst);
		auto dataSize = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
		THROW_IF_WIN32_ERROR(RegSetValueExW(hKey.get(), AUTORUN_VALUE_NAME, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), dataSize));
	}
	else
	{
		auto status = RegDeleteValueW(hKey.get(), AUTORUN_VALUE_NAME);
		THROW_HR_IF(HRESULT_FROM_WIN32(status), status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND);
	}
}

void SaveAutoStartSettingNoThrow()
{
	try
	{
		SaveAutoStartSetting();
	}
	CATCH_LOG();
}

void DefaultSettings()
{
	g_autoStart = false;
	g_reconnect = false;
	g_duckOtherApps = true;
	g_showStartupToast = false;
	g_volume = 1.0;
	g_duckedAppsVolume = 0.35;
	g_lastDevices.clear();
}

void LoadSettings()
{
	try
	{
		DefaultSettings();
		g_autoStart = LoadAutoStartSetting();

		wil::unique_hfile hFile(CreateFileW((GetModuleFsPath(g_hInst).remove_filename() / CONFIG_NAME).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
		THROW_LAST_ERROR_IF(!hFile);

		std::string string;
		while (true)
		{
			size_t size = string.size();
			string.resize(size + BUFFER_SIZE);
			DWORD read = 0;
			THROW_IF_WIN32_BOOL_FALSE(ReadFile(hFile.get(), string.data() + size, BUFFER_SIZE, &read, nullptr));
			string.resize(size + read);
			if (read == 0)
				break;
		}

		std::wstring utf16 = Utf8ToUtf16(string);
		auto jsonObj = JsonObject::Parse(utf16);

		if (jsonObj.HasKey(L"reconnect"))
			g_reconnect = jsonObj.Lookup(L"reconnect").GetBoolean();

		if (jsonObj.HasKey(L"volume"))
			g_volume = std::clamp(jsonObj.Lookup(L"volume").GetNumber(), 0.0, 1.0);

		if (jsonObj.HasKey(L"duckOtherApps"))
			g_duckOtherApps = jsonObj.Lookup(L"duckOtherApps").GetBoolean();

		if (jsonObj.HasKey(L"duckedAppsVolume"))
			g_duckedAppsVolume = std::clamp(jsonObj.Lookup(L"duckedAppsVolume").GetNumber(), 0.0, 1.0);

		if (jsonObj.HasKey(L"showStartupToast"))
			g_showStartupToast = jsonObj.Lookup(L"showStartupToast").GetBoolean();

		if (jsonObj.HasKey(L"lastDevices"))
		{
			auto lastDevices = jsonObj.Lookup(L"lastDevices").GetArray();
			g_lastDevices.reserve(lastDevices.Size());
			for (const auto& i : lastDevices)
			{
				if (i.ValueType() == JsonValueType::String)
					g_lastDevices.push_back(std::wstring(i.GetString()));
			}
		}
	}
	CATCH_LOG();
}

void SaveSettings()
{
	try
	{
		SaveAutoStartSetting();

		JsonObject jsonObj;
		jsonObj.Insert(L"autoStart", JsonValue::CreateBooleanValue(g_autoStart));
		jsonObj.Insert(L"reconnect", JsonValue::CreateBooleanValue(g_reconnect));
		jsonObj.Insert(L"volume", JsonValue::CreateNumberValue(g_volume));
		jsonObj.Insert(L"duckOtherApps", JsonValue::CreateBooleanValue(g_duckOtherApps));
		jsonObj.Insert(L"duckedAppsVolume", JsonValue::CreateNumberValue(g_duckedAppsVolume));
		jsonObj.Insert(L"showStartupToast", JsonValue::CreateBooleanValue(g_showStartupToast));

		JsonArray lastDevices;
		for (const auto& i : g_audioPlaybackConnections)
		{
			lastDevices.Append(JsonValue::CreateStringValue(i.first));
		}
		jsonObj.Insert(L"lastDevices", lastDevices);

		wil::unique_hfile hFile(CreateFileW((GetModuleFsPath(g_hInst).remove_filename() / CONFIG_NAME).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		THROW_LAST_ERROR_IF(!hFile);

		std::string utf8 = Utf16ToUtf8(jsonObj.Stringify());
		DWORD written = 0;
		THROW_IF_WIN32_BOOL_FALSE(WriteFile(hFile.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr));
		THROW_HR_IF(E_FAIL, written != utf8.size());
	}
	CATCH_LOG();
}
