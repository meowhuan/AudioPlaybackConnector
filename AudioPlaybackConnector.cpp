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
void ScheduleOutputDeviceRoutingAttempt(std::wstring_view, std::wstring_view, std::chrono::milliseconds, bool);
void ApplyOwnSessionVolume(std::wstring_view);
void ApplyDuckingPolicy();
void RestoreDuckedSessions();
void ApplyOutputDeviceRouting(std::wstring_view);
void ShowInitialToastNotification();

namespace
{
	constexpr auto RECONNECT_COOLDOWN = std::chrono::milliseconds(1500);
	constexpr auto DISCONNECT_TIMEOUT = std::chrono::milliseconds(2000);
	constexpr auto OUTPUT_ROUTING_DELAY = std::chrono::milliseconds(1500);
	constexpr auto OUTPUT_ROUTING_SETTINGS_DELAY = std::chrono::milliseconds(600);
	constexpr auto OUTPUT_ROUTING_POST_APPLY_DELAY = std::chrono::milliseconds(250);
	constexpr uint32_t OUTPUT_ROUTING_MAX_ATTEMPTS = 1;
	constexpr wchar_t DEFAULT_OUTPUT_DEVICE_ITEM_ID[] = L"";
	constexpr wchar_t DEFAULT_OUTPUT_DEVICE_ITEM_NAME[] = L"Default output device";
	constexpr wchar_t UNAVAILABLE_OUTPUT_DEVICE_ITEM_PREFIX[] = L"[Unavailable] ";

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

	bool IsCurrentConnection(std::wstring_view deviceId, AudioPlaybackConnection const& connection)
	{
		auto it = g_audioPlaybackConnections.find(std::wstring(deviceId));
		return it != g_audioPlaybackConnections.end() && it->second.second == connection;
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

	void LogOutputRoutingAttempt(std::wstring_view reason, std::wstring_view source, uint32_t attempt, uint64_t generation, bool skipped)
	{
		wchar_t buffer[512];
		swprintf_s(
			buffer,
			L"[AudioPlaybackConnector] route-attempt reason=%.*s source=%.*s attempt=%lu generation=%llu connections=%zu skipped=%d\r\n",
			static_cast<int>(reason.size()),
			reason.data(),
			static_cast<int>(source.size()),
			source.data(),
			static_cast<unsigned long>(attempt),
			static_cast<unsigned long long>(generation),
			g_audioPlaybackConnections.size(),
			skipped ? 1 : 0
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

	winrt::com_ptr<IMMDevice> GetActiveRenderDevice()
	{
		bool fellBackToDefault = false;
		auto resolvedDevice = ResolvePreferredOutputDevice(&fellBackToDefault);
		THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_NOT_FOUND), !resolvedDevice.has_value());

		auto deviceEnumerator = CreateDeviceEnumerator();
		winrt::com_ptr<IMMDevice> device;
		THROW_IF_FAILED(deviceEnumerator->GetDevice(resolvedDevice->id.c_str(), device.put()));
		return device;
	}

	winrt::com_ptr<IAudioSessionManager2> CreateTargetSessionManager()
	{
		auto device = GetActiveRenderDevice();
		winrt::com_ptr<IAudioSessionManager2> sessionManager;
		THROW_IF_FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, sessionManager.put_void()));
		return sessionManager;
	}

	winrt::fire_and_forget TryApplyDelayedOutputRouting(std::wstring reason, std::wstring source, std::chrono::milliseconds delay, uint64_t generation, uint64_t scheduledToken, uint32_t attempt)
	{
		co_await winrt::resume_after(delay);

		if (scheduledToken != g_outputRoutingScheduledToken || generation != g_outputRoutingGeneration)
		{
			LogOutputRoutingAttempt(reason, source, attempt, generation, true);
			co_return;
		}

		if (g_audioPlaybackConnections.empty() || attempt > OUTPUT_ROUTING_MAX_ATTEMPTS)
		{
			LogOutputRoutingAttempt(reason, source, attempt, generation, true);
			co_return;
		}

		LogOutputRoutingAttempt(reason, source, attempt, generation, false);
		ApplyOutputDeviceRouting(reason);
		co_await winrt::resume_after(OUTPUT_ROUTING_POST_APPLY_DELAY);

		if (scheduledToken != g_outputRoutingScheduledToken || generation != g_outputRoutingGeneration || g_audioPlaybackConnections.empty())
		{
			co_return;
		}

		ApplyOwnSessionVolume(L"delayed-route");
		ApplyDuckingPolicy();
	}
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);
	UNREFERENCED_PARAMETER(nCmdShow);

	g_hInst = hInstance;
	LoadTranslateData();

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
			PostMessageW(hExistingWnd, WM_SHOW_DEVICEPICKER_FROM_OTHER_INSTANCE, 0, 0);
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

		supported = ApiInformation::IsTypePresent(winrt::name_of<DesktopWindowXamlSource>()) &&
			ApiInformation::IsTypePresent(winrt::name_of<AudioPlaybackConnection>());
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

	DesktopWindowXamlSource desktopSource;
	auto desktopSourceNative2 = desktopSource.as<IDesktopWindowXamlSourceNative2>();
	winrt::check_hresult(desktopSourceNative2->AttachToWindow(g_hWnd));
	winrt::check_hresult(desktopSourceNative2->get_WindowHandle(&g_hWndXaml));

	g_xamlCanvas = Canvas();
	desktopSource.Content(g_xamlCanvas);

	LoadSettings();
	SetupFlyout();
	SetupSettingsFlyout();
	SetupMenu();
	SetupDevicePicker();
	SetupSvgIcon();

	g_nid.hWnd = g_niid.hWnd = g_hWnd;
	wcscpy_s(g_nid.szTip, _(L"AudioPlaybackConnector"));
	UpdateNotifyIcon();

	WM_TASKBAR_CREATED = RegisterWindowMessageW(L"TaskbarCreated");
	LOG_LAST_ERROR_IF(WM_TASKBAR_CREATED == 0);

	PostMessageW(g_hWnd, WM_CONNECTDEVICE, 0, 0);
	ShowInitialToastNotification();

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0))
	{
		BOOL processed = FALSE;
		winrt::check_hresult(desktopSourceNative2->PreTranslateMessage(&msg, &processed));
		if (!processed)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
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
		Shell_NotifyIconW(NIM_DELETE, &g_nid);
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
			ShowDevicePickerFromTray();
			break;
		case WM_RBUTTONUP:
			g_menuFocusState = FocusState::Pointer;
			break;
		case WM_CONTEXTMENU:
		{
			if (g_menuFocusState == FocusState::Unfocused)
				g_menuFocusState = FocusState::Keyboard;

			auto dpi = GetDpiForWindow(hWnd);
			Point point = {
				static_cast<float>(GET_X_LPARAM(wParam) * USER_DEFAULT_SCREEN_DPI / dpi),
				static_cast<float>(GET_Y_LPARAM(wParam) * USER_DEFAULT_SCREEN_DPI / dpi)
			};

			SetWindowPos(g_hWndXaml, 0, 0, 0, 0, 0, SWP_NOZORDER | SWP_SHOWWINDOW);
			SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, 1, 1, SWP_SHOWWINDOW);
			SetForegroundWindow(hWnd);

			g_xamlMenu.ShowAt(g_xamlCanvas, point);
		}
		break;
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

void ScheduleOutputDeviceRoutingAttempt(std::wstring_view reason, std::wstring_view source, std::chrono::milliseconds delay, bool resetAttemptBudget)
{
	if (resetAttemptBudget)
	{
		g_outputRoutingAttemptCount = 0;
		++g_outputRoutingGeneration;
	}

	if (g_audioPlaybackConnections.empty())
	{
		LogOutputRoutingAttempt(reason, source, 0, g_outputRoutingGeneration, true);
		return;
	}

	if (g_outputRoutingAttemptCount >= OUTPUT_ROUTING_MAX_ATTEMPTS)
	{
		LogOutputRoutingAttempt(reason, source, g_outputRoutingAttemptCount, g_outputRoutingGeneration, true);
		return;
	}

	const auto attempt = ++g_outputRoutingAttemptCount;
	const auto generation = g_outputRoutingGeneration;
	const auto token = ++g_outputRoutingScheduledToken;
	TryApplyDelayedOutputRouting(std::wstring(reason), std::wstring(source), delay, generation, token, attempt);
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
	});
	g_autoStartCheckBox.Unchecked([](const auto&, const auto&) {
		g_autoStart = false;
		SaveAutoStartSettingNoThrow();
	});

	TextBlock outputDeviceLabel;
	outputDeviceLabel.Text(_(L"Output device"));

	g_outputDeviceComboBox = ComboBox();
	g_outputDeviceComboBox.Width(320);
	g_outputDeviceComboBox.SelectionChanged([](const auto& sender, const auto&) {
		if (g_isRefreshingOutputDeviceComboBox)
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
			LogRoutingDiagnostics(L"settings-output-device-pending", g_outputDeviceId, false);
			return;
		}

		ScheduleOutputDeviceRoutingAttempt(L"settings-output-device", L"settings-change", OUTPUT_ROUTING_SETTINGS_DELAY, true);
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
	});

	g_volumeValueText = TextBlock();
	UpdateVolumeText();

	g_duckOtherAppsCheckBox = CheckBox();
	g_duckOtherAppsCheckBox.IsChecked(g_duckOtherApps);
	g_duckOtherAppsCheckBox.Content(winrt::box_value(_(L"Reduce other apps while connected")));
	g_duckOtherAppsCheckBox.Checked([](const auto&, const auto&) {
		g_duckOtherApps = true;
		ApplyDuckingPolicy();
	});
	g_duckOtherAppsCheckBox.Unchecked([](const auto&, const auto&) {
		g_duckOtherApps = false;
		RestoreDuckedSessions();
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
	});

	g_duckedAppsVolumeValueText = TextBlock();
	UpdateDuckedAppsVolumeText();

	g_showStartupToastCheckBox = CheckBox();
	g_showStartupToastCheckBox.IsChecked(g_showStartupToast);
	g_showStartupToastCheckBox.Content(winrt::box_value(_(L"Show startup notification")));
	g_showStartupToastCheckBox.Checked([](const auto&, const auto&) {
		g_showStartupToast = true;
	});
	g_showStartupToastCheckBox.Unchecked([](const auto&, const auto&) {
		g_showStartupToast = false;
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
		auto lastClose = g_lastCloseTime.find(deviceId);
		if (lastClose != g_lastCloseTime.end())
		{
			const auto elapsed = Clock::now() - lastClose->second;
			if (elapsed < RECONNECT_COOLDOWN)
			{
				co_await winrt::resume_after(std::chrono::duration_cast<std::chrono::milliseconds>(RECONNECT_COOLDOWN - elapsed));
			}
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

			connection.StateChanged([deviceId, source](const auto& sender, const auto&) {
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
					if (!g_outputDeviceId.empty())
					{
						ScheduleOutputDeviceRoutingAttempt(L"state-opened-delayed-route", source, OUTPUT_ROUTING_DELAY, false);
					}
					ApplyOwnSessionVolume(L"state-opened");
					ApplyDuckingPolicy();
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
						++g_outputRoutingGeneration;
						g_outputRoutingAttemptCount = 0;
						++g_outputRoutingScheduledToken;
						g_activeOutputDeviceId.clear();
						ApplyOwnSessionVolume(L"state-closed-empty");
						RestoreDuckedSessions();
					}
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
		ApplyOwnSessionVolume(L"connect-success");
		ApplyDuckingPolicy();
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
			++g_outputRoutingGeneration;
			g_outputRoutingAttemptCount = 0;
			++g_outputRoutingScheduledToken;
			g_activeOutputDeviceId.clear();
			ApplyOwnSessionVolume(L"connect-failure-empty");
			RestoreDuckedSessions();
		}
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
			++g_outputRoutingGeneration;
			g_outputRoutingAttemptCount = 0;
			++g_outputRoutingScheduledToken;
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
		++g_outputRoutingGeneration;
		g_outputRoutingAttemptCount = 0;
		++g_outputRoutingScheduledToken;
		g_activeOutputDeviceId.clear();
		ApplyOwnSessionVolume(L"disconnect-empty");
		RestoreDuckedSessions();
	}
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
