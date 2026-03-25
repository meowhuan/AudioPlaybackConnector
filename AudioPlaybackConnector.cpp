#include "pch.h"
#include "AudioPlaybackConnector.h"

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void SetupFlyout();
void SetupSettingsFlyout();
void SetupMenu();
winrt::fire_and_forget ConnectDevice(DevicePicker, std::wstring_view, std::wstring_view);
winrt::fire_and_forget ConnectDevice(DevicePicker, DeviceInformation, std::wstring_view);
winrt::fire_and_forget DisconnectDevice(DevicePicker, DeviceInformation);
winrt::fire_and_forget RefreshDeviceStatuses(DevicePicker);
void SetupDevicePicker();
void SetupSvgIcon();
void UpdateNotifyIcon();
void ShowTrayFlyout(Flyout);
void ShowDevicePickerFromTray();
void UpdateVolumeText();
void UpdateDuckedAppsVolumeText();
void RefreshOutputDeviceOptions();
void UpdateOutputDeviceSelection();
void ApplyOwnSessionVolume(std::wstring_view);
void ApplyDuckingPolicy();
void RestoreDuckedSessions();
void ApplyOutputDeviceRouting(std::wstring_view);
void SetupTrayMenu();
void ShowTrayMenu();
void ShowMainGui(std::wstring_view = L"show");
void StartBackendPipes();
void StopBackendPipes();
void PublishBackendState(std::wstring_view, bool isRunning = true);
void ReloadSettingsAndApply();
void ExecuteBackendCommand(std::wstring_view);
void SaveRuntimeStateNoThrow(std::wstring_view, bool isRunning = true);
winrt::fire_and_forget ConfirmOutputRoutingAfterOpen(std::wstring, AudioPlaybackConnection, std::wstring, bool, uint64_t);
winrt::fire_and_forget WaitForSessionThenApplyVolume(std::wstring, AudioPlaybackConnection, std::wstring);
IAsyncAction SoftReconnectAllConnectionsForOutputSwitch(uint64_t);
winrt::fire_and_forget DisconnectDeviceById(std::wstring_view);
void ShowInitialToastNotification();

namespace
{
	constexpr auto RECONNECT_COOLDOWN = std::chrono::milliseconds(1500);
	constexpr auto DISCONNECT_TIMEOUT = std::chrono::milliseconds(2000);
	constexpr auto SESSION_SNAPSHOT_DELAY = std::chrono::milliseconds(2000);
	constexpr auto PLAYBACK_PROBE_DELAY = std::chrono::milliseconds(2000);
	constexpr auto OUTPUT_CONFIRM_INTERVAL = std::chrono::milliseconds(150);
	constexpr auto OUTPUT_CONFIRM_TIMEOUT = std::chrono::milliseconds(3000);
	constexpr wchar_t DEFAULT_OUTPUT_DEVICE_ITEM_ID[] = L"";
	constexpr wchar_t DEFAULT_OUTPUT_DEVICE_ITEM_NAME[] = L"Default output device";
	constexpr wchar_t UNAVAILABLE_OUTPUT_DEVICE_ITEM_PREFIX[] = L"[Unavailable] ";
	constexpr wchar_t RUNTIME_STATE_NAME[] = L"AudioPlaybackConnector.runtime.json";
	constexpr wchar_t BACKEND_DIAGNOSTIC_LOG_NAME[] = L"AudioPlaybackConnector.backend.log";
	constexpr wchar_t COMMAND_PIPE_NAME[] = LR"(\\.\pipe\AudioPlaybackConnector.Command)";
	constexpr wchar_t EVENT_PIPE_NAME[] = LR"(\\.\pipe\AudioPlaybackConnector.Events)";
	constexpr ULONG_PTR BACKEND_COMMAND_COPYDATA_ID = 0x41504343;
	constexpr UINT ID_TRAY_OPEN_APP = 1001;
	constexpr UINT ID_TRAY_OPEN_PICKER = 1002;
	constexpr UINT ID_TRAY_AUTOSTART = 1003;
	constexpr UINT ID_TRAY_RECONNECT = 1004;
	constexpr UINT ID_TRAY_DUCK = 1005;
	constexpr UINT ID_TRAY_STARTUP_TOAST = 1006;
	constexpr UINT ID_TRAY_BLUETOOTH_SETTINGS = 1007;
	constexpr UINT ID_TRAY_EXIT = 1008;

	struct DECLSPEC_UUID("870af99c-171d-4f9e-9083-f5377e6a3555") IPolicyConfig2 : ::IUnknown
	{
		virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX**) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX**) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX*, WAVEFORMATEX*) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, INT, INT64) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
		virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY&, PROPVARIANT*) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
		virtual HRESULT STDMETHODCALLTYPE SetPerAppDefaultEndpoint(PCWSTR, DWORD, ERole) = 0;
	};

	struct DECLSPEC_UUID("870af99c-171d-4f9e-af0d-e63df40c2bc9") CPolicyConfigClient;

	struct CurrentProcessSessionLocation
	{
		std::wstring deviceId;
		std::wstring deviceName;
		std::wstring sessionId;
		AudioSessionState state = AudioSessionStateInactive;
	};

	bool IsCurrentConnection(std::wstring_view deviceId, AudioPlaybackConnection const& connection)
	{
		auto it = g_audioPlaybackConnections.find(std::wstring(deviceId));
		return it != g_audioPlaybackConnections.end() && it->second.second == connection;
	}

	std::wstring SerializeBackendCommand(std::wstring_view verb, std::wstring_view payload = {})
	{
		if (payload.empty())
		{
			return std::wstring(verb);
		}

		return std::wstring(verb) + L"\n" + std::wstring(payload);
	}

	std::pair<std::wstring, std::wstring> ParseBackendCommand(std::wstring_view command)
	{
		const auto separator = command.find(L'\n');
		if (separator == std::wstring_view::npos)
		{
			return { std::wstring(command), {} };
		}

		return {
			std::wstring(command.substr(0, separator)),
			std::wstring(command.substr(separator + 1))
		};
	}

	std::optional<std::wstring> BuildStartupBackendCommand()
	{
		int argc = 0;
		auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
		if (!argv)
		{
			return std::nullopt;
		}

		auto freeArgv = wil::scope_exit([&] { LocalFree(argv); });
		if (argc <= 1)
		{
			return std::nullopt;
		}

		const std::wstring verb = argv[1];
		if (_wcsicmp(verb.c_str(), L"picker") == 0)
		{
			return SerializeBackendCommand(L"picker");
		}

		if (_wcsicmp(verb.c_str(), L"show") == 0)
		{
			return SerializeBackendCommand(L"show-gui");
		}

		if (_wcsicmp(verb.c_str(), L"exit") == 0)
		{
			return SerializeBackendCommand(L"exit");
		}

		if (_wcsicmp(verb.c_str(), L"reload-settings") == 0)
		{
			return SerializeBackendCommand(L"reload-settings");
		}

		if ((_wcsicmp(verb.c_str(), L"connect") == 0 || _wcsicmp(verb.c_str(), L"disconnect") == 0) && argc >= 3)
		{
			return SerializeBackendCommand(verb, argv[2]);
		}

		return std::nullopt;
	}

	bool TrySendBackendCommandToWindow(HWND hWnd, std::wstring_view command)
	{
		if (!hWnd)
		{
			return false;
		}

		COPYDATASTRUCT copyData{};
		copyData.dwData = BACKEND_COMMAND_COPYDATA_ID;
		copyData.cbData = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
		copyData.lpData = const_cast<wchar_t*>(command.data());

		LRESULT result = 0;
		return SendMessageTimeoutW(
			hWnd,
			WM_COPYDATA,
			0,
			reinterpret_cast<LPARAM>(&copyData),
			SMTO_ABORTIFHUNG,
			2000,
			reinterpret_cast<PDWORD_PTR>(&result)
		) != 0;
	}

	std::optional<fs::path> ResolveRepositoryRootFromModule()
	{
		auto current = GetModuleFsPath(g_hInst).remove_filename();
		while (!current.empty())
		{
			if (fs::exists(current / L"AudioPlaybackConnector.sln") || fs::exists(current / L".git"))
			{
				return current;
			}

			if (current == current.root_path())
			{
				break;
			}
			current = current.parent_path();
		}

		return std::nullopt;
	}

	std::optional<fs::path> ResolveGuiExecutablePath()
	{
		const auto hostDirectory = GetModuleFsPath(g_hInst).remove_filename();
		const std::vector<fs::path> directCandidates = {
			hostDirectory.parent_path() / L"AudioPlaybackConnector.WinUI3.exe",
			hostDirectory / L"AudioPlaybackConnector.WinUI3.exe"
		};

		for (const auto& candidate : directCandidates)
		{
			if (fs::exists(candidate))
			{
				return candidate;
			}
		}

		const auto repoRoot = ResolveRepositoryRootFromModule();
		if (!repoRoot.has_value())
		{
			return std::nullopt;
		}

		const auto binRoot = repoRoot.value() / L"src" / L"AudioPlaybackConnector.WinUI3" / L"bin";
		if (!fs::exists(binRoot))
		{
			return std::nullopt;
		}

		const auto platformName = hostDirectory.parent_path().filename();
		const auto configurationName = hostDirectory.filename();
		const auto preferredRoot = binRoot / platformName / configurationName;
		std::vector<fs::path> matches;

		auto collect = [&](const fs::path& root) {
			if (!fs::exists(root))
			{
				return;
			}

			for (const auto& entry : fs::recursive_directory_iterator(root))
			{
				if (entry.is_regular_file() && entry.path().filename() == L"AudioPlaybackConnector.WinUI3.exe")
				{
					matches.push_back(entry.path());
				}
			}
		};

		collect(preferredRoot);
		if (matches.empty())
		{
			collect(binRoot);
		}

		if (!matches.empty())
		{
			std::sort(matches.begin(), matches.end(), [](const auto& left, const auto& right) {
				const auto leftStable = left.wstring().find(L"net8.0") != std::wstring::npos;
				const auto rightStable = right.wstring().find(L"net8.0") != std::wstring::npos;
				if (leftStable != rightStable)
				{
					return leftStable;
				}

				return fs::last_write_time(left) > fs::last_write_time(right);
			});
			return matches.front();
		}

		return std::nullopt;
	}

	bool WritePipeUtf8Message(HANDLE pipe, std::string_view utf8)
	{
		const auto payload = utf8.empty() ? std::string() : std::string(utf8);
		DWORD written = 0;
		if (!WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr))
		{
			return false;
		}

		return written == payload.size();
	}

	bool WritePipeUtf8Line(HANDLE pipe, std::string_view utf8)
	{
		std::string payload(utf8);
		payload.push_back('\n');
		return WritePipeUtf8Message(pipe, payload);
	}

	std::optional<std::string> ReadPipeUtf8Message(HANDLE pipe)
	{
		std::string message;
		char buffer[2048];

		for (;;)
		{
			DWORD read = 0;
			if (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr))
			{
				if (read == 0)
				{
					break;
				}

				message.append(buffer, read);
				break;
			}

			if (GetLastError() == ERROR_MORE_DATA)
			{
				message.append(buffer, read);
				continue;
			}

			return std::nullopt;
		}

		while (!message.empty() && (message.back() == '\0' || message.back() == '\n' || message.back() == '\r'))
		{
			message.pop_back();
		}

		return message;
	}

	void CloseAllBackendEventClients()
	{
		std::lock_guard lock(g_backendEventPipeClientsMutex);
		for (auto pipe : g_backendEventPipeClients)
		{
			if (pipe)
			{
				DisconnectNamedPipe(pipe);
				CloseHandle(pipe);
			}
		}
		g_backendEventPipeClients.clear();
	}

	void BroadcastBackendEventUtf8(std::string_view utf8)
	{
		std::lock_guard lock(g_backendEventPipeClientsMutex);
		for (auto it = g_backendEventPipeClients.begin(); it != g_backendEventPipeClients.end();)
		{
			if (!WritePipeUtf8Line(*it, utf8))
			{
				DisconnectNamedPipe(*it);
				CloseHandle(*it);
				it = g_backendEventPipeClients.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void AppendBackendDiagnosticLog(std::wstring_view line)
	{
		const auto path = GetModuleFsPath(g_hInst).remove_filename() / BACKEND_DIAGNOSTIC_LOG_NAME;
		wil::unique_hfile hFile(CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
		if (!hFile)
		{
			return;
		}

		const auto message = Utf16ToUtf8(std::wstring(line) + L"\r\n");
		DWORD written = 0;
		WriteFile(hFile.get(), message.data(), static_cast<DWORD>(message.size()), &written, nullptr);
	}

	void WakeBackendPipeServers()
	{
		auto wakePipe = [](const wchar_t* pipeName) {
			wil::unique_handle client(CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
			if (client)
			{
				const char newline = '\n';
				DWORD written = 0;
				WriteFile(client.get(), &newline, 1, &written, nullptr);
			}
		};

		wakePipe(COMMAND_PIPE_NAME);
		wakePipe(EVENT_PIPE_NAME);
	}

	void BackendCommandPipeThreadProc()
	{
		while (WaitForSingleObject(g_backendPipeStopEvent, 0) == WAIT_TIMEOUT)
		{
			wil::unique_handle pipe(CreateNamedPipeW(
				COMMAND_PIPE_NAME,
				PIPE_ACCESS_DUPLEX,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
				PIPE_UNLIMITED_INSTANCES,
				4096,
				4096,
				0,
				nullptr
			));
			if (!pipe)
			{
				Sleep(250);
				continue;
			}

			const bool connected = ConnectNamedPipe(pipe.get(), nullptr) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
			if (!connected)
			{
				continue;
			}

			if (WaitForSingleObject(g_backendPipeStopEvent, 0) != WAIT_TIMEOUT)
			{
				DisconnectNamedPipe(pipe.get());
				break;
			}

			const auto utf8Command = ReadPipeUtf8Message(pipe.get());
			if (utf8Command.has_value() && !utf8Command->empty())
			{
				auto command = new std::wstring(Utf8ToUtf16(*utf8Command));
				if (!command->empty() && (*command)[0] == 0xFEFF)
				{
					command->erase(command->begin());
				}
				if (!PostMessageW(g_hWnd, WM_BACKEND_PIPE_COMMAND, 0, reinterpret_cast<LPARAM>(command)))
				{
					delete command;
				}
			}

			WritePipeUtf8Line(pipe.get(), R"({"accepted":true})");
			FlushFileBuffers(pipe.get());
			DisconnectNamedPipe(pipe.get());
		}
	}

	void BackendEventPipeThreadProc()
	{
		while (WaitForSingleObject(g_backendPipeStopEvent, 0) == WAIT_TIMEOUT)
		{
			auto rawPipe = CreateNamedPipeW(
				EVENT_PIPE_NAME,
				PIPE_ACCESS_OUTBOUND,
				PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
				PIPE_UNLIMITED_INSTANCES,
				4096,
				4096,
				0,
				nullptr
			);
			if (rawPipe == INVALID_HANDLE_VALUE)
			{
				Sleep(250);
				continue;
			}

			const bool connected = ConnectNamedPipe(rawPipe, nullptr) != FALSE || GetLastError() == ERROR_PIPE_CONNECTED;
			if (!connected)
			{
				CloseHandle(rawPipe);
				continue;
			}

			if (WaitForSingleObject(g_backendPipeStopEvent, 0) != WAIT_TIMEOUT)
			{
				DisconnectNamedPipe(rawPipe);
				CloseHandle(rawPipe);
				break;
			}

			{
				std::lock_guard lock(g_backendEventPipeClientsMutex);
				g_backendEventPipeClients.push_back(rawPipe);
			}

			auto command = new std::wstring(SerializeBackendCommand(L"publish-state"));
			if (!PostMessageW(g_hWnd, WM_BACKEND_PIPE_COMMAND, 0, reinterpret_cast<LPARAM>(command)))
			{
				delete command;
			}
		}
	}

	void MarkDeviceClosed(std::wstring_view deviceId)
	{
		g_lastCloseTime.insert_or_assign(std::wstring(deviceId), Clock::now());
	}

	bool ShouldDuckOtherApps()
	{
		return g_duckOtherApps && !g_audioPlaybackConnections.empty();
	}

	void LogRoutingDiagnostics(std::wstring_view reason, std::wstring_view targetDeviceId, bool fellBackToDefault)
	{
		wchar_t buffer[512];
		swprintf_s(
			buffer,
			L"[AudioPlaybackConnector] route reason=%.*s target=%.*s active=%.*s fallback=%d preferred=%.*s\r\n",
			static_cast<int>(reason.size()),
			reason.data(),
			static_cast<int>(targetDeviceId.size()),
			targetDeviceId.data(),
			static_cast<int>(g_activeOutputDeviceId.size()),
			g_activeOutputDeviceId.data(),
			fellBackToDefault ? 1 : 0,
			static_cast<int>(g_outputDeviceId.size()),
			g_outputDeviceId.data()
		);
		OutputDebugStringW(buffer);
	}

	void LogConnectionDiagnostics(std::wstring_view phase, std::wstring_view source, std::wstring_view deviceId, AudioPlaybackConnectionState state)
	{
		wchar_t buffer[512];
		swprintf_s(
			buffer,
			L"[AudioPlaybackConnector] connection phase=%.*s source=%.*s device=%.*s state=%d reconnect=%d connections=%zu\r\n",
			static_cast<int>(phase.size()),
			phase.data(),
			static_cast<int>(source.size()),
			source.data(),
			static_cast<int>(deviceId.size()),
			deviceId.data(),
			static_cast<int>(state),
			g_reconnect ? 1 : 0,
			g_audioPlaybackConnections.size()
		);
		OutputDebugStringW(buffer);
	}

	void LogRoutingConfirmationDiagnostics(std::wstring_view phase, std::wstring_view source, std::wstring_view targetDeviceId, std::optional<CurrentProcessSessionLocation> const& location, uint64_t token)
	{
		wchar_t buffer[768];
		swprintf_s(
			buffer,
			L"[AudioPlaybackConnector] route-confirm phase=%.*s source=%.*s target=%.*s token=%llu found=%d actual=%.*s session=%.*s state=%d softReconnect=%d\r\n",
			static_cast<int>(phase.size()),
			phase.data(),
			static_cast<int>(source.size()),
			source.data(),
			static_cast<int>(targetDeviceId.size()),
			targetDeviceId.data(),
			static_cast<unsigned long long>(token),
			location.has_value() ? 1 : 0,
			location ? static_cast<int>(location->deviceId.size()) : 0,
			location ? location->deviceId.data() : L"",
			location ? static_cast<int>(location->sessionId.size()) : 0,
			location ? location->sessionId.data() : L"",
			location ? static_cast<int>(location->state) : -1,
			g_outputSwitchSoftReconnectInProgress ? 1 : 0
		);
		OutputDebugStringW(buffer);
	}

	std::wstring GetSessionKey(IAudioSessionControl2* sessionControl2)
	{
		wil::unique_cotaskmem_string sessionId;
		if (SUCCEEDED(sessionControl2->GetSessionInstanceIdentifier(wil::out_param(sessionId))) && sessionId)
		{
			return std::wstring(sessionId.get());
		}

		DWORD processId = 0;
		if (FAILED(sessionControl2->GetProcessId(&processId)))
		{
			processId = 0;
		}

		wchar_t buffer[32];
		swprintf_s(buffer, L"pid:%lu", processId);
		return buffer;
	}

	void LogVolumeDiagnostics(std::wstring_view reason)
	{
		wchar_t buffer[256];
		swprintf_s(
			buffer,
			L"[AudioPlaybackConnector] volume reason=%.*s target=%d mute=%d connections=%zu duck=%d\r\n",
			static_cast<int>(reason.size()),
			reason.data(),
			static_cast<int>(std::lround(g_volume * 100.0)),
			g_volume <= 0.0 ? 1 : 0,
			g_audioPlaybackConnections.size(),
			g_duckOtherApps ? 1 : 0
		);
		OutputDebugStringW(buffer);
	}

	const wchar_t* ToSessionStateString(AudioSessionState state)
	{
		switch (state)
		{
		case AudioSessionStateActive:
			return L"active";
		case AudioSessionStateInactive:
			return L"inactive";
		case AudioSessionStateExpired:
			return L"expired";
		default:
			return L"unknown";
		}
	}

	std::wstring ReadOptionalSessionString(IAudioSessionControl* sessionControl, HRESULT (STDMETHODCALLTYPE IAudioSessionControl::* getter)(LPWSTR*))
	{
		wil::unique_cotaskmem_string value;
		if (SUCCEEDED((sessionControl->*getter)(wil::out_param(value))) && value)
		{
			return std::wstring(value.get());
		}
		return {};
	}

	std::wstring QueryProcessImagePath(DWORD processId)
	{
		if (processId == 0)
		{
			return {};
		}

		wil::unique_handle processHandle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
		if (!processHandle)
		{
			return {};
		}

		std::wstring path(MAX_PATH, L'\0');
		for (;;)
		{
			DWORD size = static_cast<DWORD>(path.size());
			if (QueryFullProcessImageNameW(processHandle.get(), 0, path.data(), &size))
			{
				path.resize(size);
				return path;
			}

			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			{
				return {};
			}

			path.resize(path.size() * 2);
		}
	}

	std::wstring GetProcessDisplayName(DWORD processId, std::wstring_view imagePath)
	{
		if (processId == 0)
		{
			return L"SystemSounds";
		}

		if (!imagePath.empty())
		{
			return fs::path(imagePath).filename().wstring();
		}

		wchar_t buffer[32];
		swprintf_s(buffer, L"pid-%lu", processId);
		return buffer;
	}

	const wchar_t* GetSessionHostKind(DWORD processId, bool isCurrentProcess, bool isSystemSounds, std::wstring_view processName, std::wstring_view processPath)
	{
		if (isCurrentProcess)
		{
			return L"current-process";
		}
		if (isSystemSounds || processId == 0)
		{
			return L"system-sounds";
		}
		if (processPath.empty())
		{
			return L"access-limited";
		}
		if (_wcsicmp(processName.data(), L"svchost.exe") == 0)
		{
			return L"service-host";
		}
		return L"external-process";
	}

	std::wstring GetDeviceId(IMMDevice* device)
	{
		wil::unique_cotaskmem_string deviceId;
		THROW_IF_FAILED(device->GetId(wil::out_param(deviceId)));
		return std::wstring(deviceId.get());
	}

	std::wstring GetDeviceFriendlyName(IMMDevice* device)
	{
		winrt::com_ptr<IPropertyStore> propertyStore;
		THROW_IF_FAILED(device->OpenPropertyStore(STGM_READ, propertyStore.put()));

		PROPVARIANT value{};
		THROW_IF_FAILED(propertyStore->GetValue(PKEY_Device_FriendlyName, &value));
		auto clearValue = wil::scope_exit([&] { PropVariantClear(&value); });

		if (value.vt == VT_LPWSTR && value.pwszVal)
		{
			return value.pwszVal;
		}

		return _(L"Unknown device");
	}

	winrt::com_ptr<IMMDeviceEnumerator> CreateDeviceEnumerator()
	{
		winrt::com_ptr<IMMDeviceEnumerator> deviceEnumerator;
		THROW_IF_FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), deviceEnumerator.put_void()));
		return deviceEnumerator;
	}

	winrt::com_ptr<IMMDevice> GetDefaultRenderDevice(IMMDeviceEnumerator* deviceEnumerator)
	{
		winrt::com_ptr<IMMDevice> device;
		THROW_IF_FAILED(deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.put()));
		return device;
	}

	std::vector<OutputDeviceInfo> EnumerateOutputDevices(std::wstring* defaultDeviceId = nullptr)
	{
		auto deviceEnumerator = CreateDeviceEnumerator();
		auto defaultDevice = GetDefaultRenderDevice(deviceEnumerator.get());
		auto resolvedDefaultDeviceId = GetDeviceId(defaultDevice.get());

		if (defaultDeviceId)
		{
			*defaultDeviceId = resolvedDefaultDeviceId;
		}

		winrt::com_ptr<IMMDeviceCollection> collection;
		THROW_IF_FAILED(deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.put()));

		UINT count = 0;
		THROW_IF_FAILED(collection->GetCount(&count));

		std::vector<OutputDeviceInfo> devices;
		devices.reserve(count);
		for (UINT i = 0; i < count; ++i)
		{
			winrt::com_ptr<IMMDevice> device;
			THROW_IF_FAILED(collection->Item(i, device.put()));

			auto deviceId = GetDeviceId(device.get());
			OutputDeviceInfo info;
			info.id = std::move(deviceId);
			info.name = GetDeviceFriendlyName(device.get());
			info.isDefault = info.id == resolvedDefaultDeviceId;
			devices.push_back(std::move(info));
		}

		std::sort(devices.begin(), devices.end(), [](const auto& left, const auto& right) {
			if (left.isDefault != right.isDefault)
			{
				return left.isDefault;
			}
			return CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
		});

		return devices;
	}

	std::optional<OutputDeviceInfo> ResolvePreferredOutputDevice(bool* fellBackToDefault = nullptr)
	{
		bool usedFallback = false;
		std::wstring defaultDeviceId;
		auto devices = EnumerateOutputDevices(&defaultDeviceId);

		if (g_outputDeviceId.empty())
		{
			auto it = std::find_if(devices.begin(), devices.end(), [&](const auto& device) {
				return device.id == defaultDeviceId;
			});
			if (it != devices.end())
			{
				if (fellBackToDefault)
				{
					*fellBackToDefault = false;
				}
				return *it;
			}
		}
		else
		{
			auto it = std::find_if(devices.begin(), devices.end(), [&](const auto& device) {
				return device.id == g_outputDeviceId;
			});
			if (it != devices.end())
			{
				if (fellBackToDefault)
				{
					*fellBackToDefault = false;
				}
				return *it;
			}
			usedFallback = true;
		}

		auto defaultIt = std::find_if(devices.begin(), devices.end(), [](const auto& device) {
			return device.isDefault;
		});
		if (fellBackToDefault)
		{
			*fellBackToDefault = usedFallback;
		}
		if (defaultIt != devices.end())
		{
			return *defaultIt;
		}
		return std::nullopt;
	}

	bool ShouldApplyPreOpenRouting(std::wstring_view source)
	{
		if (g_outputDeviceId.empty())
		{
			return false;
		}

		return source == L"output-switch-soft-reconnect";
	}

	// Returns true when we need to actively confirm that the current-process
	// audio session has migrated to the target output device and potentially
	// trigger a soft-reconnect if it has not done so within the timeout.
	bool ShouldConfirmOutputRouting(std::wstring_view source)
	{
		if (g_outputDeviceId.empty())
		{
			return false;
		}

		return source == L"output-switch-soft-reconnect"
			|| source == L"settings-output-device-change";
	}

	// Returns true when we should wait for the current-process audio session to
	// appear before applying volume / ducking, regardless of whether any output
	// routing is configured.  This prevents the race where ApplyOwnSessionVolume
	// is called before the Bluetooth audio session has been registered by the OS.
	bool ShouldWaitForSessionBeforeApplyingVolume(std::wstring_view source)
	{
		return source == L"manual-picker" || source == L"manual-command" || source == L"auto-reconnect";
	}

	bool ShouldDelayAudioProcessingUntilRoutingConfirmed(std::wstring_view source)
	{
		return ShouldConfirmOutputRouting(source) || ShouldWaitForSessionBeforeApplyingVolume(source);
	}

	std::optional<CurrentProcessSessionLocation> FindCurrentProcessSessionLocation()
	{
		auto deviceEnumerator = CreateDeviceEnumerator();
		winrt::com_ptr<IMMDeviceCollection> collection;
		THROW_IF_FAILED(deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.put()));

		UINT count = 0;
		THROW_IF_FAILED(collection->GetCount(&count));

		const auto processId = GetCurrentProcessId();
		std::optional<CurrentProcessSessionLocation> inactiveLocation;
		for (UINT deviceIndex = 0; deviceIndex < count; ++deviceIndex)
		{
			winrt::com_ptr<IMMDevice> device;
			THROW_IF_FAILED(collection->Item(deviceIndex, device.put()));

			winrt::com_ptr<IAudioSessionManager2> sessionManager;
			THROW_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));

			winrt::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
			THROW_IF_FAILED(sessionManager->GetSessionEnumerator(sessionEnumerator.put()));

			int sessionCount = 0;
			THROW_IF_FAILED(sessionEnumerator->GetCount(&sessionCount));

			for (int sessionIndex = 0; sessionIndex < sessionCount; ++sessionIndex)
			{
				winrt::com_ptr<IAudioSessionControl> sessionControl;
				THROW_IF_FAILED(sessionEnumerator->GetSession(sessionIndex, sessionControl.put()));

				auto sessionControl2 = sessionControl.try_as<IAudioSessionControl2>();
				if (!sessionControl2)
				{
					continue;
				}

				DWORD sessionProcessId = 0;
				THROW_IF_FAILED(sessionControl2->GetProcessId(&sessionProcessId));
				if (sessionProcessId != processId)
				{
					continue;
				}

				AudioSessionState sessionState = AudioSessionStateInactive;
				THROW_IF_FAILED(sessionControl->GetState(&sessionState));

				CurrentProcessSessionLocation location;
				location.deviceId = GetDeviceId(device.get());
				location.deviceName = GetDeviceFriendlyName(device.get());
				location.sessionId = GetSessionKey(sessionControl2.get());
				location.state = sessionState;
				if (sessionState == AudioSessionStateActive)
				{
					return location;
				}
				if (!inactiveLocation.has_value())
				{
					inactiveLocation = std::move(location);
				}
			}
		}

		return inactiveLocation;
	}

	void LogAudioSessionSnapshotForDevice(std::wstring_view reason, std::wstring_view label, IMMDevice* device)
	{
		try
		{
			const auto deviceId = GetDeviceId(device);
			const auto deviceName = GetDeviceFriendlyName(device);

			wchar_t header[1024];
			swprintf_s(
				header,
				L"[AudioPlaybackConnector] session-snapshot reason=%.*s label=%.*s device=%s name=%s preferred=%s active=%s pid=%lu connections=%zu\r\n",
				static_cast<int>(reason.size()),
				reason.data(),
				static_cast<int>(label.size()),
				label.data(),
				deviceId.c_str(),
				deviceName.c_str(),
				g_outputDeviceId.c_str(),
				g_activeOutputDeviceId.c_str(),
				GetCurrentProcessId(),
				g_audioPlaybackConnections.size()
			);
			OutputDebugStringW(header);

			winrt::com_ptr<IAudioSessionManager2> sessionManager;
			THROW_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));

			winrt::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
			THROW_IF_FAILED(sessionManager->GetSessionEnumerator(sessionEnumerator.put()));

			int sessionCount = 0;
			THROW_IF_FAILED(sessionEnumerator->GetCount(&sessionCount));

			for (int i = 0; i < sessionCount; ++i)
			{
				winrt::com_ptr<IAudioSessionControl> sessionControl;
				THROW_IF_FAILED(sessionEnumerator->GetSession(i, sessionControl.put()));

				auto sessionControl2 = sessionControl.try_as<IAudioSessionControl2>();
				if (!sessionControl2)
				{
					continue;
				}

				DWORD sessionProcessId = 0;
				LOG_IF_FAILED(sessionControl2->GetProcessId(&sessionProcessId));

				BOOL isMuted = FALSE;
				float sessionVolume = -1.0f;
				auto simpleAudioVolume = sessionControl.try_as<ISimpleAudioVolume>();
				if (simpleAudioVolume)
				{
					LOG_IF_FAILED(simpleAudioVolume->GetMute(&isMuted));
					LOG_IF_FAILED(simpleAudioVolume->GetMasterVolume(&sessionVolume));
				}

				AudioSessionState sessionState = AudioSessionStateInactive;
				LOG_IF_FAILED(sessionControl->GetState(&sessionState));

				const auto isSystemSounds = sessionControl2->IsSystemSoundsSession() == S_OK;
				const auto sessionId = GetSessionKey(sessionControl2.get());
				const auto displayName = ReadOptionalSessionString(sessionControl.get(), &IAudioSessionControl::GetDisplayName);
				const auto iconPath = ReadOptionalSessionString(sessionControl.get(), &IAudioSessionControl::GetIconPath);
				const auto isCurrentProcess = sessionProcessId == GetCurrentProcessId();
				const auto processPath = QueryProcessImagePath(sessionProcessId);
				const auto processName = GetProcessDisplayName(sessionProcessId, processPath);
				const auto hostKind = GetSessionHostKind(sessionProcessId, isCurrentProcess, isSystemSounds, processName, processPath);

				wchar_t line[3072];
				swprintf_s(
					line,
					L"[AudioPlaybackConnector] session reason=%.*s label=%.*s idx=%d pid=%lu process=%s host=%s current=%d system=%d state=%s mute=%d volume=%.3f session=%s display=%s icon=%s path=%s\r\n",
					static_cast<int>(reason.size()),
					reason.data(),
					static_cast<int>(label.size()),
					label.data(),
					i,
					sessionProcessId,
					processName.c_str(),
					hostKind,
					isCurrentProcess ? 1 : 0,
					isSystemSounds ? 1 : 0,
					ToSessionStateString(sessionState),
					isMuted ? 1 : 0,
					sessionVolume,
					sessionId.c_str(),
					displayName.c_str(),
					iconPath.c_str(),
					processPath.c_str()
				);
				OutputDebugStringW(line);
			}
		}
		CATCH_LOG();
	}

	void LogRelevantAudioSessionSnapshots(std::wstring_view reason)
	{
		try
		{
			auto deviceEnumerator = CreateDeviceEnumerator();
			auto defaultDevice = GetDefaultRenderDevice(deviceEnumerator.get());
			LogAudioSessionSnapshotForDevice(reason, L"default", defaultDevice.get());

			if (!g_outputDeviceId.empty())
			{
				bool fellBackToDefault = false;
				auto preferredDevice = ResolvePreferredOutputDevice(&fellBackToDefault);
				if (preferredDevice.has_value())
				{
					winrt::com_ptr<IMMDevice> device;
					THROW_IF_FAILED(deviceEnumerator->GetDevice(preferredDevice->id.c_str(), device.put()));

					if (preferredDevice->id == GetDeviceId(defaultDevice.get()))
					{
						LogRoutingDiagnostics(L"snapshot-preferred-matches-default", preferredDevice->id, fellBackToDefault);
					}
					else
					{
						LogAudioSessionSnapshotForDevice(reason, L"preferred", device.get());
					}
				}
				else
				{
					LogRoutingDiagnostics(L"snapshot-preferred-unresolved", g_outputDeviceId, true);
				}
			}
		}
		CATCH_LOG();
	}

	winrt::fire_and_forget LogRelevantAudioSessionSnapshotsAfterDelay(std::wstring reason, std::chrono::milliseconds delay, uint64_t token)
	{
		co_await winrt::resume_after(delay);
		if (token != g_outputDeviceSnapshotToken || g_audioPlaybackConnections.empty())
		{
			co_return;
		}
		LogRelevantAudioSessionSnapshots(reason);
	}

	winrt::com_ptr<IMMDevice> GetCurrentRenderDevice()
	{
		auto deviceEnumerator = CreateDeviceEnumerator();
		return GetDefaultRenderDevice(deviceEnumerator.get());
	}

	const wchar_t* ToConnectionStateString(AudioPlaybackConnectionState state)
	{
		switch (state)
		{
		case AudioPlaybackConnectionState::Opened:
			return L"opened";
		case AudioPlaybackConnectionState::Closed:
			return L"closed";
		default:
			return L"unknown";
		}
	}

	winrt::com_ptr<IAudioSessionManager2> CreateTargetSessionManager()
	{
		auto device = GetCurrentRenderDevice();
		winrt::com_ptr<IAudioSessionManager2> sessionManager;
		THROW_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));
		return sessionManager;
	}

	void LogCurrentProcessPlaybackProbe(std::wstring_view reason)
	{
		try
		{
			auto deviceEnumerator = CreateDeviceEnumerator();
			auto device = GetDefaultRenderDevice(deviceEnumerator.get());
			const auto deviceId = GetDeviceId(device.get());
			const auto deviceName = GetDeviceFriendlyName(device.get());

			winrt::com_ptr<IAudioSessionManager2> sessionManager;
			THROW_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));

			winrt::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
			THROW_IF_FAILED(sessionManager->GetSessionEnumerator(sessionEnumerator.put()));

			int sessionCount = 0;
			THROW_IF_FAILED(sessionEnumerator->GetCount(&sessionCount));

			const auto currentProcessId = GetCurrentProcessId();
			int matchedSessions = 0;
			int activeSessions = 0;

			for (int i = 0; i < sessionCount; ++i)
			{
				winrt::com_ptr<IAudioSessionControl> sessionControl;
				THROW_IF_FAILED(sessionEnumerator->GetSession(i, sessionControl.put()));

				auto sessionControl2 = sessionControl.try_as<IAudioSessionControl2>();
				if (!sessionControl2)
				{
					continue;
				}

				DWORD sessionProcessId = 0;
				THROW_IF_FAILED(sessionControl2->GetProcessId(&sessionProcessId));
				if (sessionProcessId != currentProcessId)
				{
					continue;
				}

				++matchedSessions;

				AudioSessionState sessionState = AudioSessionStateInactive;
				LOG_IF_FAILED(sessionControl->GetState(&sessionState));
				if (sessionState == AudioSessionStateActive)
				{
					++activeSessions;
				}

				const auto sessionId = GetSessionKey(sessionControl2.get());
				wchar_t line[1024];
				swprintf_s(
					line,
					L"[AudioPlaybackConnector] playback-probe reason=%.*s device=%s name=%s matched=%d active=%d idx=%d state=%s session=%s\r\n",
					static_cast<int>(reason.size()),
					reason.data(),
					deviceId.c_str(),
					deviceName.c_str(),
					matchedSessions,
					activeSessions,
					i,
					ToSessionStateString(sessionState),
					sessionId.c_str()
				);
				OutputDebugStringW(line);
			}

			wchar_t summary[1024];
			swprintf_s(
				summary,
				L"[AudioPlaybackConnector] playback-probe-summary reason=%.*s device=%s name=%s matched=%d active=%d pid=%lu connections=%zu\r\n",
				static_cast<int>(reason.size()),
				reason.data(),
				deviceId.c_str(),
				deviceName.c_str(),
				matchedSessions,
				activeSessions,
				currentProcessId,
				g_audioPlaybackConnections.size()
			);
			OutputDebugStringW(summary);
		}
		CATCH_LOG();
	}

	winrt::fire_and_forget LogCurrentProcessPlaybackProbeAfterDelay(std::wstring reason, std::wstring deviceId, std::chrono::milliseconds delay)
	{
		co_await winrt::resume_after(delay);
		auto it = g_audioPlaybackConnections.find(deviceId);
		if (it == g_audioPlaybackConnections.end() || it->second.second.State() != AudioPlaybackConnectionState::Opened)
		{
			co_return;
		}
		LogCurrentProcessPlaybackProbe(reason);
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(nCmdShow);
	UNREFERENCED_PARAMETER(lpCmdLine);

	g_hInst = hInstance;
	LoadTranslateData();
	auto startupCommand = BuildStartupBackendCommand();

	g_hMutex = CreateMutexW(nullptr, TRUE, UNIQUE_MUTEX_NAME);
	if (!g_hMutex)
	{
		TaskDialog(nullptr, nullptr, _(L"Error"), nullptr, _(L"Could not create mutex. Application will exit."), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
		return EXIT_FAILURE;
	}

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		CloseHandle(g_hMutex);
		g_hMutex = nullptr;

		HWND hExistingWnd = FindWindowW(L"AudioPlaybackConnector", nullptr);
		if (hExistingWnd)
		{
			SetForegroundWindow(hExistingWnd);
			if (startupCommand.has_value())
			{
				TrySendBackendCommandToWindow(hExistingWnd, startupCommand.value());
			}
			else
			{
				TrySendBackendCommandToWindow(hExistingWnd, SerializeBackendCommand(L"show-gui"));
			}
		}
		else
		{
			TaskDialog(nullptr, nullptr, _(L"Information"), nullptr, _(L"Another instance is running, but its window could not be found."), TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
		}
		return EXIT_SUCCESS;
	}

	winrt::init_apartment();

	bool supported = false;
	try
	{
		using namespace winrt::Windows::Foundation::Metadata;

		supported = ApiInformation::IsTypePresent(winrt::name_of<AudioPlaybackConnection>()) &&
			ApiInformation::IsTypePresent(winrt::name_of<DevicePicker>());
	}
	catch (winrt::hresult_error const&)
	{
		supported = false;
		LOG_CAUGHT_EXCEPTION();
	}
	if (!supported)
	{
		TaskDialog(nullptr, nullptr, _(L"Unsupported Operating System"), nullptr, _(L"AudioPlaybackConnector is not supported on this operating system version."), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
		return EXIT_FAILURE;
	}

	WNDCLASSEXW wcex = {
		.cbSize = sizeof(wcex),
		.lpfnWndProc = WndProc,
		.hInstance = hInstance,
		.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_AUDIOPLAYBACKCONNECTOR)),
		.hCursor = LoadCursorW(nullptr, IDC_ARROW),
		.lpszClassName = L"AudioPlaybackConnector",
		.hIconSm = wcex.hIcon
	};

	RegisterClassExW(&wcex);

	// When parent window size is 0x0 or invisible, the dpi scale of menu is incorrect. Here we set window size to 1x1 and use WS_EX_LAYERED to make window looks like invisible.
	g_hWnd = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TOPMOST, L"AudioPlaybackConnector", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
	FAIL_FAST_LAST_ERROR_IF_NULL(g_hWnd);
	FAIL_FAST_IF_WIN32_BOOL_FALSE(SetLayeredWindowAttributes(g_hWnd, 0, 0, LWA_ALPHA));

	LoadSettings();
	SetupDevicePicker();
	SetupTrayMenu();
	SetupSvgIcon();
	StartBackendPipes();

	g_nid.hWnd = g_niid.hWnd = g_hWnd;
	wcscpy_s(g_nid.szTip, _(L"AudioPlaybackConnector"));
	UpdateNotifyIcon();

	WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");
	LOG_LAST_ERROR_IF(WM_TASKBAR_CREATED == 0);
	g_startupBackendCommand = startupCommand.value_or(L"");
	SaveRuntimeStateNoThrow(L"startup");

	PostMessageW(g_hWnd, WM_CONNECTDEVICE, 0, 0);
	ShowInitialToastNotification();
	if (!g_startupBackendCommand.empty())
	{
		ExecuteBackendCommand(g_startupBackendCommand);
		g_startupBackendCommand.clear();
	}

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		RestoreDuckedSessions();
		{
			std::vector<std::pair<DeviceInformation, AudioPlaybackConnection>> connections;
			connections.reserve(g_audioPlaybackConnections.size());
			for (const auto& connection : g_audioPlaybackConnections)
			{
				connections.push_back(connection.second);
			}
			for (const auto& connection : connections)
			{
				connection.second.Close();
				g_devicePicker.SetDisplayStatus(connection.first, {}, DevicePickerDisplayStatusOptions::None);
			}
		}
		if (g_reconnect)
		{
			SaveSettings();
			g_audioPlaybackConnections.clear();
		}
		else
		{
			g_audioPlaybackConnections.clear();
			SaveSettings();
		}
		g_activeOutputDeviceId.clear();
		StopBackendPipes();
		SaveRuntimeStateNoThrow(L"shutdown", false);
		Shell_NotifyIconW(NIM_DELETE, &g_nid);
		if (g_trayMenu)
		{
			DestroyMenu(g_trayMenu);
			g_trayMenu = nullptr;
		}
		if (g_hMutex)
		{
			ReleaseMutex(g_hMutex);
			CloseHandle(g_hMutex);
			g_hMutex = nullptr;
		}
		PostQuitMessage(0);
		break;
	case WM_SHOW_DEVICEPICKER_FROM_OTHER_INSTANCE:
		ShowDevicePickerFromTray();
		break;
	case WM_BACKEND_PIPE_COMMAND:
	{
		auto command = reinterpret_cast<std::wstring*>(lParam);
		if (command)
		{
			ExecuteBackendCommand(*command);
			delete command;
			return 0;
		}
	}
	break;
	case WM_COPYDATA:
	{
		auto copyData = reinterpret_cast<COPYDATASTRUCT*>(lParam);
		if (copyData &&
			copyData->dwData == BACKEND_COMMAND_COPYDATA_ID &&
			copyData->lpData &&
			copyData->cbData >= sizeof(wchar_t))
		{
			const auto length = (copyData->cbData / sizeof(wchar_t)) - 1;
			const std::wstring_view command(static_cast<const wchar_t*>(copyData->lpData), length);
			ExecuteBackendCommand(command);
			return TRUE;
		}
	}
	break;
	case WM_SETTINGCHANGE:
		if (lParam && CompareStringOrdinal(reinterpret_cast<LPCWCH>(lParam), -1, L"ImmersiveColorSet", -1, TRUE) == CSTR_EQUAL)
		{
			UpdateNotifyIcon();
		}
		break;
	case WM_NOTIFYICON:
		switch (LOWORD(lParam))
		{
		case NIN_SELECT:
		case NIN_KEYSELECT:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
			ShowMainGui();
			break;
		case WM_RBUTTONUP:
		case WM_CONTEXTMENU:
			ShowTrayMenu();
			break;
		}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case ID_TRAY_OPEN_APP:
			ShowMainGui();
			return 0;
		case ID_TRAY_OPEN_PICKER:
			ShowDevicePickerFromTray();
			return 0;
		case ID_TRAY_AUTOSTART:
			g_autoStart = !g_autoStart;
			SaveSettings();
			SaveRuntimeStateNoThrow(L"tray-toggle-autostart");
			return 0;
		case ID_TRAY_RECONNECT:
			g_reconnect = !g_reconnect;
			SaveSettings();
			SaveRuntimeStateNoThrow(L"tray-toggle-reconnect");
			return 0;
		case ID_TRAY_DUCK:
			g_duckOtherApps = !g_duckOtherApps;
			if (g_duckOtherApps)
			{
				ApplyDuckingPolicy();
			}
			else
			{
				RestoreDuckedSessions();
			}
			SaveSettings();
			SaveRuntimeStateNoThrow(L"tray-toggle-duck");
			return 0;
		case ID_TRAY_STARTUP_TOAST:
			g_showStartupToast = !g_showStartupToast;
			SaveSettings();
			SaveRuntimeStateNoThrow(L"tray-toggle-startup-toast");
			return 0;
		case ID_TRAY_BLUETOOTH_SETTINGS:
			winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:bluetooth"));
			return 0;
		case ID_TRAY_EXIT:
			PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
			return 0;
		}
		break;
	case WM_CONNECTDEVICE:
		if (g_reconnect)
		{
			for (const auto& deviceId : g_lastDevices)
			{
				ConnectDevice(g_devicePicker, deviceId, L"auto-reconnect");
			}
			g_lastDevices.clear();
		}
		break;
	default:
		if (WM_TASKBAR_CREATED && message == WM_TASKBAR_CREATED)
		{
			UpdateNotifyIcon();
		}
		return DefWindowProcW(hWnd, message, wParam, lParam);
	}
	return 0;
}

void ShowTrayFlyout(Flyout flyout)
{
	RECT iconRect;
	auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
	if (FAILED(hr))
	{
		LOG_HR(hr);
		return;
	}

	auto dpi = GetDpiForWindow(g_hWnd);

	SetWindowPos(g_hWnd, HWND_TOPMOST, iconRect.left, iconRect.top, 0, 0, SWP_HIDEWINDOW);
	g_xamlCanvas.Width(static_cast<float>((iconRect.right - iconRect.left) * USER_DEFAULT_SCREEN_DPI / dpi));
	g_xamlCanvas.Height(static_cast<float>((iconRect.bottom - iconRect.top) * USER_DEFAULT_SCREEN_DPI / dpi));

	flyout.ShowAt(g_xamlCanvas);
}

void ShowDevicePickerFromTray()
{
	using namespace winrt::Windows::UI::Popups;

	RECT iconRect{};
	auto hr = Shell_NotifyIconGetRect(&g_niid, &iconRect);
	if (FAILED(hr))
	{
		LOG_HR(hr);
		ClientToScreen(g_hWnd, reinterpret_cast<POINT*>(&iconRect.left));
		ClientToScreen(g_hWnd, reinterpret_cast<POINT*>(&iconRect.right));
	}

	auto dpi = GetDpiForWindow(g_hWnd);
	Rect rect = {
		static_cast<float>(iconRect.left * USER_DEFAULT_SCREEN_DPI / dpi),
		static_cast<float>(iconRect.top * USER_DEFAULT_SCREEN_DPI / dpi),
		static_cast<float>((iconRect.right - iconRect.left) * USER_DEFAULT_SCREEN_DPI / dpi),
		static_cast<float>((iconRect.bottom - iconRect.top) * USER_DEFAULT_SCREEN_DPI / dpi)
	};

	if (IsRectEmpty(&iconRect) || FAILED(hr))
	{
		rect = { 100.0f, 100.0f, 300.0f, 400.0f };
	}

	SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), SWP_HIDEWINDOW);
	SetForegroundWindow(g_hWnd);
	RefreshDeviceStatuses(g_devicePicker);
	g_devicePicker.Show(rect, Placement::Above);
}

void SetupTrayMenu()
{
	if (g_trayMenu)
	{
		DestroyMenu(g_trayMenu);
	}

	g_trayMenu = CreatePopupMenu();
	FAIL_FAST_LAST_ERROR_IF_NULL(g_trayMenu);

	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_OPEN_APP, _(L"AudioPlaybackConnector"));
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_OPEN_PICKER, _(L"Open device picker"));
	AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_AUTOSTART, _(L"Launch at startup"));
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_RECONNECT, _(L"Reconnect on next start"));
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_DUCK, _(L"Reduce other apps while connected"));
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_STARTUP_TOAST, _(L"Show startup notification"));
	AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_BLUETOOTH_SETTINGS, _(L"Bluetooth Settings"));
	AppendMenuW(g_trayMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(g_trayMenu, MF_STRING, ID_TRAY_EXIT, _(L"Exit"));
}

void ShowTrayMenu()
{
	if (!g_trayMenu)
	{
		return;
	}

	CheckMenuItem(g_trayMenu, ID_TRAY_AUTOSTART, MF_BYCOMMAND | (g_autoStart ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(g_trayMenu, ID_TRAY_RECONNECT, MF_BYCOMMAND | (g_reconnect ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(g_trayMenu, ID_TRAY_DUCK, MF_BYCOMMAND | (g_duckOtherApps ? MF_CHECKED : MF_UNCHECKED));
	CheckMenuItem(g_trayMenu, ID_TRAY_STARTUP_TOAST, MF_BYCOMMAND | (g_showStartupToast ? MF_CHECKED : MF_UNCHECKED));

	POINT point{};
	if (!GetCursorPos(&point))
	{
		point.x = 100;
		point.y = 100;
	}

	SetForegroundWindow(g_hWnd);
	TrackPopupMenu(g_trayMenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0, g_hWnd, nullptr);
	PostMessageW(g_hWnd, WM_NULL, 0, 0);
}

void ShowMainGui(std::wstring_view command)
{
	const auto guiPath = ResolveGuiExecutablePath();
	if (!guiPath.has_value())
	{
		AppendBackendDiagnosticLog(L"show-gui: no gui path resolved");
		SaveRuntimeStateNoThrow(L"show-gui-no-path");
		TaskDialog(nullptr, nullptr, _(L"Error"), nullptr, _(L"Unknown error"), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
		return;
	}
	AppendBackendDiagnosticLog(L"show-gui: resolved path=" + guiPath->wstring());

	std::wstring commandLine = L"\"" + guiPath->wstring() + L"\"";
	if (!command.empty())
	{
		commandLine += L" ";
		commandLine += std::wstring(command);
	}

	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	PROCESS_INFORMATION processInfo{};
	if (!CreateProcessW(
		nullptr,
		commandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		guiPath->parent_path().c_str(),
		&startupInfo,
		&processInfo))
	{
		wchar_t reason[64];
		swprintf_s(reason, L"show-gui-fail-%lu", GetLastError());
		AppendBackendDiagnosticLog(std::wstring(L"show-gui: CreateProcess failed error=") + std::to_wstring(GetLastError()));
		SaveRuntimeStateNoThrow(reason);
		LOG_LAST_ERROR();
		TaskDialog(nullptr, nullptr, _(L"Error"), nullptr, _(L"Unknown error"), TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
		return;
	}

	CloseHandle(processInfo.hThread);
	CloseHandle(processInfo.hProcess);
	AppendBackendDiagnosticLog(std::wstring(L"show-gui: launched pid=") + std::to_wstring(processInfo.dwProcessId));
	SaveRuntimeStateNoThrow(L"show-gui-launch");
}

void StartBackendPipes()
{
	if (!g_backendPipeStopEvent)
	{
		g_backendPipeStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		FAIL_FAST_LAST_ERROR_IF_NULL(g_backendPipeStopEvent);
	}

	ResetEvent(g_backendPipeStopEvent);
	g_backendCommandPipeThread = std::thread(BackendCommandPipeThreadProc);
	g_backendEventPipeThread = std::thread(BackendEventPipeThreadProc);
}

void StopBackendPipes()
{
	if (g_backendPipeStopEvent)
	{
		SetEvent(g_backendPipeStopEvent);
		WakeBackendPipeServers();
	}

	if (g_backendCommandPipeThread.joinable())
	{
		g_backendCommandPipeThread.join();
	}
	if (g_backendEventPipeThread.joinable())
	{
		g_backendEventPipeThread.join();
	}

	CloseAllBackendEventClients();

	if (g_backendPipeStopEvent)
	{
		CloseHandle(g_backendPipeStopEvent);
		g_backendPipeStopEvent = nullptr;
	}
}

void UpdateVolumeText()
{
	if (g_volumeValueText)
	{
		wchar_t buffer[32];
		swprintf_s(buffer, L"%d%%", static_cast<int>(std::lround(g_volume * 100.0)));
		g_volumeValueText.Text(buffer);
	}
}

void UpdateDuckedAppsVolumeText()
{
	if (g_duckedAppsVolumeValueText)
	{
		wchar_t buffer[32];
		swprintf_s(buffer, L"%d%%", static_cast<int>(std::lround(g_duckedAppsVolume * 100.0)));
		g_duckedAppsVolumeValueText.Text(buffer);
	}
}

void RefreshOutputDeviceOptions()
{
	try
	{
		g_outputDevices = EnumerateOutputDevices();

		if (!g_outputDeviceComboBox)
			return;

		g_isRefreshingOutputDeviceComboBox = true;
		auto resetRefreshing = wil::scope_exit([&] { g_isRefreshingOutputDeviceComboBox = false; });

		auto items = g_outputDeviceComboBox.Items();
		items.Clear();

		ComboBoxItem defaultItem;
		defaultItem.Content(winrt::box_value(_(DEFAULT_OUTPUT_DEVICE_ITEM_NAME)));
		items.Append(defaultItem);

		bool selected = g_outputDeviceId.empty();
		for (const auto& device : g_outputDevices)
		{
			ComboBoxItem item;
			item.Content(winrt::box_value(device.name));
			items.Append(item);

			if (device.id == g_outputDeviceId)
			{
				g_outputDeviceComboBox.SelectedItem(item);
				selected = true;
			}
		}

		if (!selected && !g_outputDeviceId.empty())
		{
			ComboBoxItem unavailableItem;
			unavailableItem.Content(winrt::box_value(std::wstring(_(UNAVAILABLE_OUTPUT_DEVICE_ITEM_PREFIX)) + g_outputDeviceId));
			items.Append(unavailableItem);
			g_outputDeviceComboBox.SelectedItem(unavailableItem);
			selected = true;
		}

		if (!selected)
		{
			g_outputDeviceComboBox.SelectedIndex(0);
		}
	}
	CATCH_LOG();
}

void UpdateOutputDeviceSelection()
{
	if (!g_outputDeviceComboBox)
		return;

	g_isRefreshingOutputDeviceComboBox = true;
	g_isProgrammaticOutputDeviceSelection = true;
	auto resetRefreshing = wil::scope_exit([&] {
		g_isProgrammaticOutputDeviceSelection = false;
		g_isRefreshingOutputDeviceComboBox = false;
	});

	if (g_outputDeviceId.empty())
	{
		if (g_outputDeviceComboBox.Items().Size() > 0)
		{
			g_outputDeviceComboBox.SelectedIndex(0);
		}
		return;
	}

	for (uint32_t i = 0; i < g_outputDeviceComboBox.Items().Size(); ++i)
	{
		if (i > 0 && (i - 1) < g_outputDevices.size() && g_outputDevices[i - 1].id == g_outputDeviceId)
		{
			g_outputDeviceComboBox.SelectedIndex(static_cast<int32_t>(i));
			return;
		}
	}

	if (g_outputDeviceComboBox.Items().Size() > 0)
	{
		g_outputDeviceComboBox.SelectedIndex(0);
	}
}

void ApplyOutputDeviceRouting(std::wstring_view reason)
{
	try
	{
		bool fellBackToDefault = false;
		auto resolvedDevice = ResolvePreferredOutputDevice(&fellBackToDefault);
		if (!resolvedDevice.has_value())
		{
			g_activeOutputDeviceId.clear();
			LogRoutingDiagnostics(reason, {}, true);
			return;
		}

		winrt::com_ptr<IPolicyConfig2> policyConfig;
		THROW_IF_FAILED(CoCreateInstance(__uuidof(CPolicyConfigClient), nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig2), policyConfig.put_void()));

		const auto processId = GetCurrentProcessId();
		const auto targetDeviceId = resolvedDevice->id;
		for (const auto role : { eConsole, eMultimedia, eCommunications })
		{
			THROW_IF_FAILED(policyConfig->SetPerAppDefaultEndpoint(targetDeviceId.c_str(), processId, role));
		}

		g_activeOutputDeviceId = targetDeviceId;
		LogRoutingDiagnostics(reason, g_activeOutputDeviceId, fellBackToDefault);
	}
	CATCH_LOG();
}

void PublishBackendState(std::wstring_view reason, bool isRunning)
{
	JsonObject jsonObj;
	jsonObj.Insert(L"running", JsonValue::CreateBooleanValue(isRunning));
	jsonObj.Insert(L"reason", JsonValue::CreateStringValue(std::wstring(reason)));
	jsonObj.Insert(L"processId", JsonValue::CreateNumberValue(GetCurrentProcessId()));
	jsonObj.Insert(L"reconnect", JsonValue::CreateBooleanValue(g_reconnect));
	jsonObj.Insert(L"volume", JsonValue::CreateNumberValue(g_volume));
	jsonObj.Insert(L"duckOtherApps", JsonValue::CreateBooleanValue(g_duckOtherApps));
	jsonObj.Insert(L"duckedAppsVolume", JsonValue::CreateNumberValue(g_duckedAppsVolume));
	jsonObj.Insert(L"showStartupToast", JsonValue::CreateBooleanValue(g_showStartupToast));
	jsonObj.Insert(L"outputDeviceId", JsonValue::CreateStringValue(g_outputDeviceId));
	jsonObj.Insert(L"activeOutputDeviceId", JsonValue::CreateStringValue(g_activeOutputDeviceId));
	jsonObj.Insert(L"softReconnectInProgress", JsonValue::CreateBooleanValue(g_outputSwitchSoftReconnectInProgress));

	JsonArray connections;
	for (const auto& [deviceId, value] : g_audioPlaybackConnections)
	{
		JsonObject connection;
		connection.Insert(L"id", JsonValue::CreateStringValue(deviceId));
		connection.Insert(L"name", JsonValue::CreateStringValue(std::wstring(value.first.Name())));
		connection.Insert(L"state", JsonValue::CreateStringValue(ToConnectionStateString(value.second.State())));
		connection.Insert(L"stateValue", JsonValue::CreateNumberValue(static_cast<int>(value.second.State())));
		connections.Append(connection);
	}
	jsonObj.Insert(L"connections", connections);

	const auto utf16 = jsonObj.Stringify();
	const auto utf8 = Utf16ToUtf8(utf16);

	const auto path = GetModuleFsPath(g_hInst).remove_filename() / RUNTIME_STATE_NAME;
	wil::unique_hfile hFile(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	THROW_LAST_ERROR_IF(!hFile);

	DWORD written = 0;
	THROW_IF_WIN32_BOOL_FALSE(WriteFile(hFile.get(), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr));
	THROW_HR_IF(E_FAIL, written != utf8.size());

	BroadcastBackendEventUtf8(utf8);
}

void SaveRuntimeStateNoThrow(std::wstring_view reason, bool isRunning)
{
	try
	{
		PublishBackendState(reason, isRunning);
	}
	CATCH_LOG();
}

void ReloadSettingsAndApply()
{
	const auto previousOutputDeviceId = g_outputDeviceId;

	LoadSettings();

	if (g_volumeSlider)
	{
		g_volumeSlider.Value(g_volume * 100.0);
	}
	if (g_duckOtherAppsCheckBox)
	{
		g_duckOtherAppsCheckBox.IsChecked(g_duckOtherApps);
	}
	if (g_duckedAppsVolumeSlider)
	{
		g_duckedAppsVolumeSlider.Value(g_duckedAppsVolume * 100.0);
	}
	if (g_showStartupToastCheckBox)
	{
		g_showStartupToastCheckBox.IsChecked(g_showStartupToast);
	}
	UpdateVolumeText();
	UpdateDuckedAppsVolumeText();
	RefreshOutputDeviceOptions();
	UpdateOutputDeviceSelection();

	ApplyOwnSessionVolume(L"external-reload-settings");
	if (g_duckOtherApps)
	{
		ApplyDuckingPolicy();
	}
	else
	{
		RestoreDuckedSessions();
	}

	if (previousOutputDeviceId != g_outputDeviceId && !g_audioPlaybackConnections.empty())
	{
		const auto snapshotToken = ++g_outputDeviceSnapshotToken;
		const auto routingToken = ++g_outputRoutingToken;
		LogRoutingDiagnostics(L"external-settings-pre-route", g_outputDeviceId, false);
		ApplyOutputDeviceRouting(L"external-settings-pre-route");
		LogRelevantAudioSessionSnapshotsAfterDelay(L"external-settings-change-delayed", SESSION_SNAPSHOT_DELAY, snapshotToken);

		for (const auto& [connectedDeviceId, value] : g_audioPlaybackConnections)
		{
			if (value.second && value.second.State() == AudioPlaybackConnectionState::Opened)
			{
				ConfirmOutputRoutingAfterOpen(connectedDeviceId, value.second, L"external-settings-change", true, routingToken);
			}
		}
	}

	SaveRuntimeStateNoThrow(L"reload-settings");
}

void ExecuteBackendCommand(std::wstring_view serializedCommand)
{
	const auto [verb, payload] = ParseBackendCommand(serializedCommand);

	if (_wcsicmp(verb.c_str(), L"picker") == 0)
	{
		ShowDevicePickerFromTray();
		return;
	}

	if (_wcsicmp(verb.c_str(), L"show-gui") == 0)
	{
		ShowMainGui();
		return;
	}

	if (_wcsicmp(verb.c_str(), L"exit") == 0)
	{
		PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
		return;
	}

	if (_wcsicmp(verb.c_str(), L"reload-settings") == 0)
	{
		ReloadSettingsAndApply();
		return;
	}

	if (_wcsicmp(verb.c_str(), L"publish-state") == 0)
	{
		SaveRuntimeStateNoThrow(L"publish-state");
		return;
	}

	if (_wcsicmp(verb.c_str(), L"connect") == 0 && !payload.empty())
	{
		ConnectDevice(g_devicePicker, payload, L"manual-command");
		return;
	}

	if (_wcsicmp(verb.c_str(), L"disconnect") == 0 && !payload.empty())
	{
		DisconnectDeviceById(payload);
		return;
	}
}

// Waits until the current-process audio session appears on any active render
// device, then applies volume and ducking. Used for manual-picker and
// auto-reconnect sources to avoid the race where ApplyOwnSessionVolume is
// called before the Bluetooth audio session has been registered by the OS.
//
// This path intentionally stays on the stable playback mainline: it only waits
// for session presence and does not try to confirm or repair output-device
// routing. Hot output switching remains isolated to the dedicated
// settings-output-device-change / output-switch-soft-reconnect flow.
winrt::fire_and_forget WaitForSessionThenApplyVolume(std::wstring deviceId, AudioPlaybackConnection connection, std::wstring source)
{
	const auto startedAt = Clock::now();

	while (Clock::now() - startedAt < OUTPUT_CONFIRM_TIMEOUT)
	{
		if (!IsCurrentConnection(deviceId, connection) || connection.State() != AudioPlaybackConnectionState::Opened)
		{
			co_return;
		}

		std::optional<CurrentProcessSessionLocation> location;
		try
		{
			location = FindCurrentProcessSessionLocation();
		}
		CATCH_LOG();

		wchar_t probeBuf[768];
		swprintf_s(
			probeBuf,
			L"[AudioPlaybackConnector] session-wait source=%.*s device=%.*s found=%d sessionDevice=%.*s active=%d\r\n",
			static_cast<int>(source.size()), source.data(),
			static_cast<int>(deviceId.size()), deviceId.data(),
			location.has_value() ? 1 : 0,
			location ? static_cast<int>(location->deviceId.size()) : 0,
			location ? location->deviceId.data() : L"",
			location ? (location->state == AudioSessionStateActive ? 1 : 0) : 0
		);
		OutputDebugStringW(probeBuf);

		if (location.has_value())
		{
			ApplyOwnSessionVolume(source + L"-session-wait-success");
			ApplyDuckingPolicy();
			LogCurrentProcessPlaybackProbe(source + L"-session-wait-success");
			LogCurrentProcessPlaybackProbeAfterDelay(source + L"-playback-probe", deviceId, PLAYBACK_PROBE_DELAY);
			co_return;
		}

		co_await winrt::resume_after(OUTPUT_CONFIRM_INTERVAL);
	}

	// Timed out waiting for the session to appear. Apply volume as a best-effort
	// fallback so the user is not left with a silent connection.
	if (!IsCurrentConnection(deviceId, connection) || connection.State() != AudioPlaybackConnectionState::Opened)
	{
		co_return;
	}

	wchar_t timeoutBuf[512];
	swprintf_s(
		timeoutBuf,
		L"[AudioPlaybackConnector] session-wait-timeout source=%.*s device=%.*s\r\n",
		static_cast<int>(source.size()), source.data(),
		static_cast<int>(deviceId.size()), deviceId.data()
	);
	OutputDebugStringW(timeoutBuf);

	ApplyOwnSessionVolume(source + L"-session-wait-timeout-fallback");
	ApplyDuckingPolicy();
	LogCurrentProcessPlaybackProbe(source + L"-session-wait-timeout-fallback");
}

winrt::fire_and_forget ConfirmOutputRoutingAfterOpen(std::wstring deviceId, AudioPlaybackConnection connection, std::wstring source, bool allowSoftReconnect, uint64_t token)
{
	const auto startedAt = Clock::now();
	bool routeApplied = false;

	while (Clock::now() - startedAt < OUTPUT_CONFIRM_TIMEOUT)
	{
		if (token != g_outputRoutingToken || !IsCurrentConnection(deviceId, connection) || connection.State() != AudioPlaybackConnectionState::Opened)
		{
			co_return;
		}

		std::optional<CurrentProcessSessionLocation> location;
		try
		{
			location = FindCurrentProcessSessionLocation();
		}
		CATCH_LOG();

		LogRoutingConfirmationDiagnostics(L"confirm-check", source, g_outputDeviceId, location, token);

		if (location.has_value() &&
			location->deviceId == g_activeOutputDeviceId &&
			!g_activeOutputDeviceId.empty() &&
			location->state == AudioSessionStateActive)
		{
			LogRoutingConfirmationDiagnostics(L"confirm-active-success", source, g_outputDeviceId, location, token);
			ApplyOwnSessionVolume(L"confirm-active-success");
			ApplyDuckingPolicy();
			co_return;
		}

		if (!routeApplied)
		{
			ApplyOutputDeviceRouting(source == L"output-switch-soft-reconnect" ? L"post-open-confirm-route-soft-reconnect" : L"post-open-confirm-route");
			routeApplied = true;
		}

		co_await winrt::resume_after(OUTPUT_CONFIRM_INTERVAL);
	}

	if (token != g_outputRoutingToken || !IsCurrentConnection(deviceId, connection) || connection.State() != AudioPlaybackConnectionState::Opened)
	{
		co_return;
	}

	LogRoutingConfirmationDiagnostics(L"confirm-timeout", source, g_outputDeviceId, {}, token);

	if (allowSoftReconnect && !g_outputSwitchSoftReconnectInProgress)
	{
		g_outputSwitchSoftReconnectInProgress = true;
		// Re-read g_outputRoutingToken here rather than forwarding the caller's
		// token: ConnectDevice (called inside SoftReconnectAllConnectionsForOutputSwitch)
		// does NOT advance the token for the "output-switch-soft-reconnect" source
		// anymore, so g_outputRoutingToken at this point is the correct sentinel
		// value to detect a subsequent user-initiated change during the disconnect
		// wait.
		co_await SoftReconnectAllConnectionsForOutputSwitch(g_outputRoutingToken);
		g_outputSwitchSoftReconnectInProgress = false;
	}
	else
	{
		// Could not do a soft reconnect (either not allowed or already in progress).
		// Apply volume as a best-effort fallback.
		ApplyOwnSessionVolume(L"confirm-timeout-no-reconnect-fallback");
		ApplyDuckingPolicy();
	}
}

IAsyncAction SoftReconnectAllConnectionsForOutputSwitch(uint64_t token)
{
	if (g_audioPlaybackConnections.empty())
	{
		co_return;
	}

	std::vector<DeviceInformation> devices;
	devices.reserve(g_audioPlaybackConnections.size());
	for (const auto& [deviceId, value] : g_audioPlaybackConnections)
	{
		devices.push_back(value.first);
	}

	for (const auto& device : devices)
	{
		const auto currentDeviceId = std::wstring(device.Id());
		auto it = g_audioPlaybackConnections.find(currentDeviceId);
		if (it == g_audioPlaybackConnections.end())
		{
			continue;
		}

		LogConnectionDiagnostics(L"output-switch-soft-reconnect-close", L"settings-change", currentDeviceId, it->second.second.State());
		MarkDeviceClosed(currentDeviceId);
		g_devicePicker.SetDisplayStatus(device, _(L"Reconnecting"), DevicePickerDisplayStatusOptions::ShowProgress | DevicePickerDisplayStatusOptions::ShowDisconnectButton);
		it->second.second.Close();
	}

	co_await winrt::resume_after(DISCONNECT_TIMEOUT);

	// Abort if a newer user-initiated routing change arrived while we were
	// waiting for devices to disconnect.  ConnectDevice for the
	// "output-switch-soft-reconnect" source intentionally does NOT advance
	// g_outputRoutingToken (see the comment inside ConnectDevice), so the token
	// we captured just before calling this function remains stable across all
	// re-open calls below.
	if (token != g_outputRoutingToken)
	{
		co_return;
	}

	ApplyOutputDeviceRouting(L"output-switch-soft-reconnect");

	for (const auto& device : devices)
	{
		LogConnectionDiagnostics(L"output-switch-soft-reconnect-open", L"settings-change", std::wstring(device.Id()), AudioPlaybackConnectionState::Closed);
		ConnectDevice(g_devicePicker, device, L"output-switch-soft-reconnect");
	}
}

void ApplyOwnSessionVolume(std::wstring_view reason)
{
	try
	{
		LogVolumeDiagnostics(reason);

		auto sessionManager = CreateTargetSessionManager();
		winrt::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
		THROW_IF_FAILED(sessionManager->GetSessionEnumerator(sessionEnumerator.put()));

		int sessionCount = 0;
		THROW_IF_FAILED(sessionEnumerator->GetCount(&sessionCount));

		const auto processId = GetCurrentProcessId();
		for (int i = 0; i < sessionCount; ++i)
		{
			winrt::com_ptr<IAudioSessionControl> sessionControl;
			THROW_IF_FAILED(sessionEnumerator->GetSession(i, sessionControl.put()));

			auto sessionControl2 = sessionControl.try_as<IAudioSessionControl2>();
			if (!sessionControl2)
				continue;

			DWORD sessionProcessId = 0;
			THROW_IF_FAILED(sessionControl2->GetProcessId(&sessionProcessId));
			if (sessionProcessId != processId)
				continue;

			auto simpleAudioVolume = sessionControl.try_as<ISimpleAudioVolume>();
			if (!simpleAudioVolume)
				continue;

			const auto mute = g_volume <= 0.0;
			THROW_IF_FAILED(simpleAudioVolume->SetMute(mute ? TRUE : FALSE, nullptr));
			THROW_IF_FAILED(simpleAudioVolume->SetMasterVolume(static_cast<float>(g_volume), nullptr));
		}
	}
	CATCH_LOG();
}

void RestoreDuckedSessions()
{
	for (auto& [sessionKey, sessionInfo] : g_duckedSessions)
	{
		if (!sessionInfo.volume)
			continue;

		LOG_IF_FAILED(sessionInfo.volume->SetMasterVolume(sessionInfo.originalVolume, nullptr));
		LOG_IF_FAILED(sessionInfo.volume->SetMute(sessionInfo.originalMute, nullptr));
	}
	g_duckedSessions.clear();
}

void ApplyDuckingPolicy()
{
	try
	{
		if (!ShouldDuckOtherApps())
		{
			RestoreDuckedSessions();
			return;
		}

		auto sessionManager = CreateTargetSessionManager();
		winrt::com_ptr<IAudioSessionEnumerator> sessionEnumerator;
		THROW_IF_FAILED(sessionManager->GetSessionEnumerator(sessionEnumerator.put()));

		int sessionCount = 0;
		THROW_IF_FAILED(sessionEnumerator->GetCount(&sessionCount));

		std::unordered_set<std::wstring> seenSessions;
		const auto currentProcessId = GetCurrentProcessId();

		for (int i = 0; i < sessionCount; ++i)
		{
			winrt::com_ptr<IAudioSessionControl> sessionControl;
			THROW_IF_FAILED(sessionEnumerator->GetSession(i, sessionControl.put()));

			auto sessionControl2 = sessionControl.try_as<IAudioSessionControl2>();
			if (!sessionControl2)
				continue;

			if (sessionControl2->IsSystemSoundsSession() == S_OK)
				continue;

			DWORD sessionProcessId = 0;
			THROW_IF_FAILED(sessionControl2->GetProcessId(&sessionProcessId));
			if (sessionProcessId == currentProcessId)
				continue;

			auto simpleAudioVolume = sessionControl.try_as<ISimpleAudioVolume>();
			if (!simpleAudioVolume)
				continue;

			auto sessionKey = GetSessionKey(sessionControl2.get());
			seenSessions.insert(sessionKey);

			auto existing = g_duckedSessions.find(sessionKey);
			if (existing == g_duckedSessions.end())
			{
				float originalVolume = 1.0f;
				BOOL originalMute = FALSE;
				THROW_IF_FAILED(simpleAudioVolume->GetMasterVolume(&originalVolume));
				THROW_IF_FAILED(simpleAudioVolume->GetMute(&originalMute));

				DuckedSessionInfo sessionInfo;
				sessionInfo.volume = simpleAudioVolume;
				sessionInfo.originalVolume = originalVolume;
				sessionInfo.originalMute = originalMute;
				g_duckedSessions.emplace(sessionKey, std::move(sessionInfo));
			}
			else
			{
				existing->second.volume = simpleAudioVolume;
			}

			THROW_IF_FAILED(simpleAudioVolume->SetMasterVolume(static_cast<float>(g_duckedAppsVolume), nullptr));
		}

		for (auto it = g_duckedSessions.begin(); it != g_duckedSessions.end();)
		{
			if (seenSessions.find(it->first) == seenSessions.end())
			{
				if (it->second.volume)
				{
					LOG_IF_FAILED(it->second.volume->SetMasterVolume(it->second.originalVolume, nullptr));
					LOG_IF_FAILED(it->second.volume->SetMute(it->second.originalMute, nullptr));
				}
				it = g_duckedSessions.erase(it);
			}
			else
			{
				++it;
			}
		}
	}
	CATCH_LOG();
}

void ShowInitialToastNotification()
{
	if (!g_showStartupToast)
		return;

	try
	{
		std::wstring title = _(L"AudioPlaybackConnector");
		std::wstring message = _(L"Application has started and is running in the notification area.");

		std::wstring toastXmlString =
			L"<toast>"
			L"<visual>"
			L"<binding template=\"ToastGeneric\">"
			L"<text>" + title + L"</text>"
			L"<text>" + message + L"</text>"
			L"</binding>"
			L"</visual>"
			L"</toast>";

		XmlDocument toastXml;
		toastXml.LoadXml(toastXmlString);

		ToastNotifier notifier{ nullptr };
		try
		{
			notifier = ToastNotificationManager::CreateToastNotifier();
		}
		catch (winrt::hresult_error const&)
		{
			LOG_CAUGHT_EXCEPTION();
		}

		if (!notifier)
			return;

		ToastNotification toast(toastXml);
		notifier.Show(toast);
	}
	catch (winrt::hresult_error const&)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

void SetupFlyout()
{
	TextBlock textBlock;
	textBlock.Text(_(L"All connections will be closed.\nExit anyway?"));
	textBlock.Margin({ 0, 0, 0, 12 });

	static CheckBox checkbox;
	checkbox.IsChecked(g_reconnect);
	checkbox.Content(winrt::box_value(_(L"Reconnect on next start")));

	Button button;
	button.Content(winrt::box_value(_(L"Exit")));
	button.HorizontalAlignment(HorizontalAlignment::Right);
	button.Click([](const auto&, const auto&) {
		g_reconnect = checkbox.IsChecked().Value();
		PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
	});

	StackPanel stackPanel;
	stackPanel.Children().Append(textBlock);
	stackPanel.Children().Append(checkbox);
	stackPanel.Children().Append(button);

	Flyout flyout;
	flyout.ShouldConstrainToRootBounds(false);
	flyout.Content(stackPanel);

	g_xamlFlyout = flyout;
}

void SetupSettingsFlyout()
{
	g_autoStartCheckBox = CheckBox();
	g_autoStartCheckBox.IsChecked(g_autoStart);
	g_autoStartCheckBox.Content(winrt::box_value(_(L"Launch at startup")));
	g_autoStartCheckBox.Checked([](const auto&, const auto&) {
		g_autoStart = true;
		SaveAutoStartSettingNoThrow();
		SaveRuntimeStateNoThrow(L"autostart-enabled");
	});
	g_autoStartCheckBox.Unchecked([](const auto&, const auto&) {
		g_autoStart = false;
		SaveAutoStartSettingNoThrow();
		SaveRuntimeStateNoThrow(L"autostart-disabled");
	});

	TextBlock outputDeviceLabel;
	outputDeviceLabel.Text(_(L"Output device"));

	g_outputDeviceComboBox = ComboBox();
	g_outputDeviceComboBox.Width(320);
	g_outputDeviceComboBox.SelectionChanged([](const auto& sender, const auto&) {
		if (g_isRefreshingOutputDeviceComboBox || g_isProgrammaticOutputDeviceSelection)
			return;

		auto comboBox = sender.try_as<ComboBox>();
		if (!comboBox)
			return;

		const auto selectedIndex = comboBox.SelectedIndex();
		if (selectedIndex < 0)
			return;

		if (selectedIndex == 0)
		{
			g_outputDeviceId.clear();
		}
		else
		{
			const auto deviceIndex = static_cast<size_t>(selectedIndex - 1);
			if (deviceIndex >= g_outputDevices.size())
				return;
			g_outputDeviceId = g_outputDevices[deviceIndex].id;
		}
		if (g_audioPlaybackConnections.empty())
		{
			LogRoutingDiagnostics(L"settings-output-device-preference", g_outputDeviceId, false);
			SaveRuntimeStateNoThrow(L"output-device-preference");
			return;
		}

		if (g_outputDeviceId.empty())
		{
			LogRoutingDiagnostics(L"settings-output-device-preference", g_outputDeviceId, false);
			SaveRuntimeStateNoThrow(L"output-device-cleared");
			return;
		}

		const auto snapshotToken = ++g_outputDeviceSnapshotToken;
		const auto routingToken = ++g_outputRoutingToken;
		LogRoutingDiagnostics(L"settings-output-device-pre-route", g_outputDeviceId, false);
		ApplyOutputDeviceRouting(L"settings-output-device-pre-route");
		LogRoutingDiagnostics(L"settings-output-device-diagnostics", g_outputDeviceId, false);
		LogRelevantAudioSessionSnapshotsAfterDelay(L"settings-output-device-change-delayed", SESSION_SNAPSHOT_DELAY, snapshotToken);

		for (const auto& [connectedDeviceId, value] : g_audioPlaybackConnections)
		{
			if (value.second && value.second.State() == AudioPlaybackConnectionState::Opened)
			{
				// allowSoftReconnect=true: if the session has not migrated to
				// the target device within OUTPUT_CONFIRM_TIMEOUT we perform a
				// soft reconnect.  ConfirmOutputRoutingAfterOpen re-reads
				// g_outputRoutingToken when it kicks off the soft reconnect so
				// it is not affected by the token advancement inside ConnectDevice.
				ConfirmOutputRoutingAfterOpen(connectedDeviceId, value.second, L"settings-output-device-change", true, routingToken);
			}
		}
		SaveRuntimeStateNoThrow(L"output-device-changed");
	});
	RefreshOutputDeviceOptions();
	UpdateOutputDeviceSelection();

	TextBlock volumeLabel;
	volumeLabel.Text(_(L"Playback volume"));

	g_volumeSlider = Slider();
	g_volumeSlider.Minimum(0);
	g_volumeSlider.Maximum(100);
	g_volumeSlider.StepFrequency(1);
	g_volumeSlider.Value(g_volume * 100.0);
	g_volumeSlider.Width(220);
	g_volumeSlider.ValueChanged([](const auto&, const RangeBaseValueChangedEventArgs& args) {
		g_volume = std::clamp(args.NewValue() / 100.0, 0.0, 1.0);
		UpdateVolumeText();
		ApplyOwnSessionVolume(L"settings-slider");
		SaveRuntimeStateNoThrow(L"volume-changed");
	});

	g_volumeValueText = TextBlock();
	UpdateVolumeText();

	g_duckOtherAppsCheckBox = CheckBox();
	g_duckOtherAppsCheckBox.IsChecked(g_duckOtherApps);
	g_duckOtherAppsCheckBox.Content(winrt::box_value(_(L"Reduce other apps while connected")));
	g_duckOtherAppsCheckBox.Checked([](const auto&, const auto&) {
		g_duckOtherApps = true;
		ApplyDuckingPolicy();
		SaveRuntimeStateNoThrow(L"ducking-enabled");
	});
	g_duckOtherAppsCheckBox.Unchecked([](const auto&, const auto&) {
		g_duckOtherApps = false;
		RestoreDuckedSessions();
		SaveRuntimeStateNoThrow(L"ducking-disabled");
	});

	TextBlock duckedAppsLabel;
	duckedAppsLabel.Text(_(L"Other apps volume"));

	g_duckedAppsVolumeSlider = Slider();
	g_duckedAppsVolumeSlider.Minimum(0);
	g_duckedAppsVolumeSlider.Maximum(100);
	g_duckedAppsVolumeSlider.StepFrequency(1);
	g_duckedAppsVolumeSlider.Value(g_duckedAppsVolume * 100.0);
	g_duckedAppsVolumeSlider.Width(220);
	g_duckedAppsVolumeSlider.ValueChanged([](const auto&, const RangeBaseValueChangedEventArgs& args) {
		g_duckedAppsVolume = std::clamp(args.NewValue() / 100.0, 0.0, 1.0);
		UpdateDuckedAppsVolumeText();
		ApplyDuckingPolicy();
		SaveRuntimeStateNoThrow(L"ducked-volume-changed");
	});

	g_duckedAppsVolumeValueText = TextBlock();
	UpdateDuckedAppsVolumeText();

	g_showStartupToastCheckBox = CheckBox();
	g_showStartupToastCheckBox.IsChecked(g_showStartupToast);
	g_showStartupToastCheckBox.Content(winrt::box_value(_(L"Show startup notification")));
	g_showStartupToastCheckBox.Checked([](const auto&, const auto&) {
		g_showStartupToast = true;
		SaveRuntimeStateNoThrow(L"startup-toast-enabled");
	});
	g_showStartupToastCheckBox.Unchecked([](const auto&, const auto&) {
		g_showStartupToast = false;
		SaveRuntimeStateNoThrow(L"startup-toast-disabled");
	});

	StackPanel rootPanel;
	rootPanel.Spacing(12);
	rootPanel.Children().Append(g_autoStartCheckBox);
	rootPanel.Children().Append(outputDeviceLabel);
	rootPanel.Children().Append(g_outputDeviceComboBox);
	rootPanel.Children().Append(volumeLabel);
	rootPanel.Children().Append(g_volumeSlider);
	rootPanel.Children().Append(g_volumeValueText);
	rootPanel.Children().Append(g_duckOtherAppsCheckBox);
	rootPanel.Children().Append(duckedAppsLabel);
	rootPanel.Children().Append(g_duckedAppsVolumeSlider);
	rootPanel.Children().Append(g_duckedAppsVolumeValueText);
	rootPanel.Children().Append(g_showStartupToastCheckBox);

	Flyout flyout;
	flyout.ShouldConstrainToRootBounds(false);
	flyout.Content(rootPanel);
	flyout.Opening([](const auto&, const auto&) {
		g_isRefreshingOutputDeviceComboBox = true;
		g_isProgrammaticOutputDeviceSelection = true;
		auto resetRefreshing = wil::scope_exit([&] {
			g_isProgrammaticOutputDeviceSelection = false;
			g_isRefreshingOutputDeviceComboBox = false;
		});
		RefreshOutputDeviceOptions();
		UpdateOutputDeviceSelection();
	});

	g_settingsFlyout = flyout;
}

void SetupMenu()
{
	FontIcon settingsIcon;
	settingsIcon.Glyph(L"\xE713");

	MenuFlyoutItem settingsItem;
	settingsItem.Text(_(L"Settings"));
	settingsItem.Icon(settingsIcon);
	settingsItem.Click([](const auto&, const auto&) {
		ShowTrayFlyout(g_settingsFlyout);
	});

	FontIcon bluetoothIcon;
	bluetoothIcon.Glyph(L"\xE701");

	MenuFlyoutItem bluetoothSettingsItem;
	bluetoothSettingsItem.Text(_(L"Bluetooth Settings"));
	bluetoothSettingsItem.Icon(bluetoothIcon);
	bluetoothSettingsItem.Click([](const auto&, const auto&) {
		winrt::Windows::System::Launcher::LaunchUriAsync(Uri(L"ms-settings:bluetooth"));
	});

	FontIcon closeIcon;
	closeIcon.Glyph(L"\xE8BB");

	MenuFlyoutItem exitItem;
	exitItem.Text(_(L"Exit"));
	exitItem.Icon(closeIcon);
	exitItem.Click([](const auto&, const auto&) {
		if (g_audioPlaybackConnections.empty())
		{
			PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
			return;
		}

		ShowTrayFlyout(g_xamlFlyout);
	});

	MenuFlyout menu;
	menu.Items().Append(settingsItem);
	menu.Items().Append(bluetoothSettingsItem);
	menu.Items().Append(MenuFlyoutSeparator());
	menu.Items().Append(exitItem);
	menu.Opened([](const auto& sender, const auto&) {
		auto menuItems = sender.as<MenuFlyout>().Items();
		auto itemsCount = menuItems.Size();
		if (itemsCount > 0)
		{
			menuItems.GetAt(itemsCount - 1).Focus(g_menuFocusState);
		}
		g_menuFocusState = FocusState::Unfocused;
	});
	menu.Closed([](const auto&, const auto&) {
		ShowWindow(g_hWnd, SW_HIDE);
	});

	g_xamlMenu = menu;
}

winrt::fire_and_forget ConnectDevice(DevicePicker picker, DeviceInformation device, std::wstring_view connectionSource)
{
	picker.SetDisplayStatus(device, _(L"Connecting"), DevicePickerDisplayStatusOptions::ShowProgress | DevicePickerDisplayStatusOptions::ShowDisconnectButton);

	bool success = false;
	std::wstring errorMessage;
	const auto deviceId = std::wstring(device.Id());
	const auto source = std::wstring(connectionSource);
	AudioPlaybackConnection connection = nullptr;

	try
	{
		LogConnectionDiagnostics(L"connect-begin", source, deviceId, AudioPlaybackConnectionState::Closed);

		uint64_t routingToken = 0;
		if (ShouldApplyPreOpenRouting(source) || ShouldConfirmOutputRouting(source))
		{
			// For "output-switch-soft-reconnect" the token is owned by
			// SoftReconnectAllConnectionsForOutputSwitch which captured it just
			// before calling us.  Incrementing it here would invalidate the
			// post-disconnect guard in that function, so we read the current
			// value without advancing it.
			if (source != L"output-switch-soft-reconnect")
			{
				routingToken = ++g_outputRoutingToken;
			}
			else
			{
				routingToken = g_outputRoutingToken;
			}
		}

		auto lastClose = g_lastCloseTime.find(deviceId);
		if (lastClose != g_lastCloseTime.end())
		{
			const auto elapsed = Clock::now() - lastClose->second;
			if (elapsed < RECONNECT_COOLDOWN)
			{
				co_await winrt::resume_after(std::chrono::duration_cast<std::chrono::milliseconds>(RECONNECT_COOLDOWN - elapsed));
			}
		}

		if (routingToken != 0 && ShouldApplyPreOpenRouting(source))
		{
			ApplyOutputDeviceRouting(L"pre-open-route");
		}

		connection = AudioPlaybackConnection::TryCreateFromId(device.Id());
		if (connection)
		{
			auto existing = g_audioPlaybackConnections.find(deviceId);
			if (existing != g_audioPlaybackConnections.end())
			{
				MarkDeviceClosed(deviceId);
				existing->second.second.Close();
				g_audioPlaybackConnections.erase(existing);
			}

			g_audioPlaybackConnections.insert_or_assign(deviceId, std::pair(device, connection));

			connection.StateChanged([deviceId, source, routingToken](const auto& sender, const auto&) {
				const auto state = sender.State();
				LogConnectionDiagnostics(L"state-changed", source, deviceId, state);
				if (state == AudioPlaybackConnectionState::Opened)
				{
					if (IsCurrentConnection(deviceId, sender))
					{
						auto it = g_audioPlaybackConnections.find(deviceId);
						if (it != g_audioPlaybackConnections.end())
						{
							g_devicePicker.SetDisplayStatus(it->second.first, _(L"Connected"), DevicePickerDisplayStatusOptions::ShowDisconnectButton);
						}
					}
					if (routingToken != 0 && ShouldConfirmOutputRouting(source))
					{
						// Confirm that the session migrated to the target device.
						// allowSoftReconnect=false here because the soft-reconnect
						// path (settings-output-device-change -> ConfirmOutputRoutingAfterOpen
						// with allowSoftReconnect=true) is handled on the connect-success
						// path below, not inside StateChanged.
						ConfirmOutputRoutingAfterOpen(deviceId, sender, source, false, routingToken);
					}
					else if (ShouldWaitForSessionBeforeApplyingVolume(source))
					{
						// Wait for the OS to register the Bluetooth audio session before
						// applying volume so we don't silently lose the volume set.
						WaitForSessionThenApplyVolume(deviceId, sender, source);
					}
					else
					{
						ApplyOwnSessionVolume(L"state-opened");
						ApplyDuckingPolicy();
					}
					SaveRuntimeStateNoThrow(L"connection-opened");
				}
				else if (state == AudioPlaybackConnectionState::Closed)
				{
					MarkDeviceClosed(deviceId);
					if (IsCurrentConnection(deviceId, sender))
					{
						auto it = g_audioPlaybackConnections.find(deviceId);
						if (it != g_audioPlaybackConnections.end())
						{
							g_devicePicker.SetDisplayStatus(it->second.first, {}, DevicePickerDisplayStatusOptions::None);
							g_audioPlaybackConnections.erase(it);
						}
					}
					sender.Close();
					if (g_audioPlaybackConnections.empty())
					{
						g_activeOutputDeviceId.clear();
						ApplyOwnSessionVolume(L"state-closed-empty");
						RestoreDuckedSessions();
					}
					SaveRuntimeStateNoThrow(L"connection-closed");
				}
			});

			co_await connection.StartAsync();
			auto result = co_await connection.OpenAsync();

			switch (result.Status())
			{
			case AudioPlaybackConnectionOpenResultStatus::Success:
				success = true;
				break;
			case AudioPlaybackConnectionOpenResultStatus::RequestTimedOut:
				errorMessage = _(L"The request timed out");
				break;
			case AudioPlaybackConnectionOpenResultStatus::DeniedBySystem:
				errorMessage = _(L"The operation was denied by the system");
				break;
			case AudioPlaybackConnectionOpenResultStatus::UnknownFailure:
				winrt::throw_hresult(result.ExtendedError());
				break;
			}
		}
		else
		{
			errorMessage = _(L"Unknown error");
		}
	}
	catch (winrt::hresult_error const& ex)
	{
		errorMessage.resize(64);
		while (true)
		{
			auto result = swprintf(errorMessage.data(), errorMessage.size(), L"%s (0x%08X)", ex.message().c_str(), static_cast<uint32_t>(ex.code()));
			if (result < 0)
			{
				errorMessage.resize(errorMessage.size() * 2);
			}
			else
			{
				errorMessage.resize(result);
				break;
			}
		}
		LOG_CAUGHT_EXCEPTION();
	}

	if (success)
	{
		LogConnectionDiagnostics(L"open-success", source, deviceId, connection ? connection.State() : AudioPlaybackConnectionState::Closed);
		if (connection && connection.State() == AudioPlaybackConnectionState::Opened)
		{
			picker.SetDisplayStatus(device, _(L"Connected"), DevicePickerDisplayStatusOptions::ShowDisconnectButton);
		}
		else
		{
			picker.SetDisplayStatus(device, _(L"Open requested"), DevicePickerDisplayStatusOptions::ShowDisconnectButton);
		}
		if (!ShouldDelayAudioProcessingUntilRoutingConfirmed(source))
		{
			ApplyOwnSessionVolume(L"connect-success");
			ApplyDuckingPolicy();
		}
		else if (source == L"manual-picker" || source == L"auto-reconnect")
		{
			// Volume / ducking will be applied once the session appears; the
			// WaitForSessionThenApplyVolume coroutine launched from StateChanged
			// handles this.  Log a probe at the open-success point for correlation.
			LogCurrentProcessPlaybackProbe(source == L"manual-picker" ? L"manual-open-success" : L"auto-reconnect-open-success");
		}
		else
		{
			// output-switch-soft-reconnect and settings-output-device-change:
			// volume / ducking are handled by ConfirmOutputRoutingAfterOpen once
			// the session has migrated to the target device.
			LogCurrentProcessPlaybackProbe(L"routed-open-success");
		}
		SaveRuntimeStateNoThrow(L"connect-success");
	}
	else
	{
		LogConnectionDiagnostics(L"open-failure", source, deviceId, connection ? connection.State() : AudioPlaybackConnectionState::Closed);
		if (connection)
		{
			if (IsCurrentConnection(deviceId, connection))
			{
				MarkDeviceClosed(deviceId);
				auto it = g_audioPlaybackConnections.find(deviceId);
				if (it != g_audioPlaybackConnections.end())
				{
					it->second.second.Close();
					g_audioPlaybackConnections.erase(it);
				}
			}
			else
			{
				connection.Close();
			}
		}
		if (g_audioPlaybackConnections.empty())
		{
			g_activeOutputDeviceId.clear();
			ApplyOwnSessionVolume(L"connect-failure-empty");
			RestoreDuckedSessions();
		}
		SaveRuntimeStateNoThrow(L"connect-failure");
		picker.SetDisplayStatus(device, errorMessage, DevicePickerDisplayStatusOptions::ShowRetryButton);
	}
}

winrt::fire_and_forget DisconnectDevice(DevicePicker picker, DeviceInformation device)
{
	const auto deviceId = std::wstring(device.Id());

	auto it = g_audioPlaybackConnections.find(deviceId);
	if (it == g_audioPlaybackConnections.end())
	{
		picker.SetDisplayStatus(device, {}, DevicePickerDisplayStatusOptions::None);
		if (g_audioPlaybackConnections.empty())
		{
			g_activeOutputDeviceId.clear();
			ApplyOwnSessionVolume(L"disconnect-missing-empty");
			RestoreDuckedSessions();
		}
		co_return;
	}

	auto connection = it->second.second;
	MarkDeviceClosed(deviceId);
	picker.SetDisplayStatus(device, {}, DevicePickerDisplayStatusOptions::None);
	connection.Close();

	co_await winrt::resume_after(DISCONNECT_TIMEOUT);
	if (IsCurrentConnection(deviceId, connection))
	{
		g_audioPlaybackConnections.erase(deviceId);
		picker.SetDisplayStatus(device, {}, DevicePickerDisplayStatusOptions::None);
	}

	if (g_audioPlaybackConnections.empty())
	{
		g_activeOutputDeviceId.clear();
		ApplyOwnSessionVolume(L"disconnect-empty");
		RestoreDuckedSessions();
	}
	SaveRuntimeStateNoThrow(L"disconnect");
}

winrt::fire_and_forget RefreshDeviceStatuses(DevicePicker picker)
{
	try
	{
		auto devices = co_await DeviceInformation::FindAllAsync(AudioPlaybackConnection::GetDeviceSelector());
		for (const auto& device : devices)
		{
			auto it = g_audioPlaybackConnections.find(std::wstring(device.Id()));
			if (it != g_audioPlaybackConnections.end())
			{
				picker.SetDisplayStatus(device, _(L"Connected"), DevicePickerDisplayStatusOptions::ShowDisconnectButton);
			}
			else
			{
				picker.SetDisplayStatus(device, {}, DevicePickerDisplayStatusOptions::None);
			}
		}
	}
	catch (winrt::hresult_error const&)
	{
		LOG_CAUGHT_EXCEPTION();
	}
}

winrt::fire_and_forget ConnectDevice(DevicePicker picker, std::wstring_view deviceId, std::wstring_view connectionSource)
{
	auto device = co_await DeviceInformation::CreateFromIdAsync(deviceId);
	ConnectDevice(picker, device, connectionSource);
}

winrt::fire_and_forget DisconnectDeviceById(std::wstring_view deviceId)
{
	auto device = co_await DeviceInformation::CreateFromIdAsync(deviceId);
	DisconnectDevice(g_devicePicker, device);
}

void SetupDevicePicker()
{
	g_devicePicker = DevicePicker();
	winrt::check_hresult(g_devicePicker.as<IInitializeWithWindow>()->Initialize(g_hWnd));

	g_devicePicker.Filter().SupportedDeviceSelectors().Append(AudioPlaybackConnection::GetDeviceSelector());
	g_devicePicker.DevicePickerDismissed([](const auto&, const auto&) {
		SetWindowPos(g_hWnd, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_HIDEWINDOW);
	});
	g_devicePicker.DeviceSelected([](const auto& sender, const auto& args) {
		ConnectDevice(sender, args.SelectedDevice(), L"manual-picker");
	});
	g_devicePicker.DisconnectButtonClicked([](const auto& sender, const auto& args) {
		DisconnectDevice(sender, args.Device());
	});
}

void SetupSvgIcon()
{
	auto hRes = FindResourceW(g_hInst, MAKEINTRESOURCEW(1), L"SVG");
	FAIL_FAST_LAST_ERROR_IF_NULL(hRes);

	auto size = SizeofResource(g_hInst, hRes);
	FAIL_FAST_LAST_ERROR_IF(size == 0);

	auto hResData = LoadResource(g_hInst, hRes);
	FAIL_FAST_LAST_ERROR_IF_NULL(hResData);

	auto svgData = reinterpret_cast<const char*>(LockResource(hResData));
	FAIL_FAST_IF_NULL_ALLOC(svgData);

	const std::string_view svg(svgData, size);
	const int width = GetSystemMetrics(SM_CXSMICON), height = GetSystemMetrics(SM_CYSMICON);

	g_hIconLight = SvgTohIcon(svg, width, height, { 0, 0, 0, 1 });
	g_hIconDark = SvgTohIcon(svg, width, height, { 1, 1, 1, 1 });
}

void UpdateNotifyIcon()
{
	DWORD value = 0, cbValue = sizeof(value);
	LOG_IF_WIN32_ERROR(RegGetValueW(HKEY_CURRENT_USER, LR"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)", L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &cbValue));
	g_nid.hIcon = value != 0 ? g_hIconLight : g_hIconDark;

	if (!Shell_NotifyIconW(NIM_MODIFY, &g_nid))
	{
		if (Shell_NotifyIconW(NIM_ADD, &g_nid))
		{
			FAIL_FAST_IF_WIN32_BOOL_FALSE(Shell_NotifyIconW(NIM_SETVERSION, &g_nid));
		}
		else
		{
			LOG_LAST_ERROR();
		}
	}
}
