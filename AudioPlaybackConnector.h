#pragma once

#include "resource.h"

using namespace winrt::Windows::Data::Json;
using namespace winrt::Windows::Data::Xml::Dom;
using namespace winrt::Windows::Devices::Enumeration;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Media::Audio;
using namespace winrt::Windows::UI::Notifications;
using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::UI::Xaml::Controls::Primitives;
using namespace winrt::Windows::UI::Xaml::Hosting;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

constexpr UINT WM_NOTIFYICON = WM_APP + 1;
constexpr UINT WM_CONNECTDEVICE = WM_APP + 2;
constexpr UINT WM_SHOW_DEVICEPICKER_FROM_OTHER_INSTANCE = WM_APP + 3;
constexpr UINT WM_BACKEND_PIPE_COMMAND = WM_APP + 4;

constexpr wchar_t UNIQUE_MUTEX_NAME[] = L"{019730ef-fcc8-7f5a-94b3-8b77d764a65f}";

struct DuckedSessionInfo
{
	winrt::com_ptr<ISimpleAudioVolume> volume;
	float originalVolume = 1.0f;
	BOOL originalMute = FALSE;
};

struct OutputDeviceInfo
{
	std::wstring id;
	std::wstring name;
	bool isDefault = false;
};

HANDLE g_hMutex = nullptr;
HINSTANCE g_hInst;
HWND g_hWnd;
HWND g_hWndXaml;
Canvas g_xamlCanvas = nullptr;
Flyout g_xamlFlyout = nullptr;
Flyout g_settingsFlyout = nullptr;
MenuFlyout g_xamlMenu = nullptr;
FocusState g_menuFocusState = FocusState::Unfocused;
DevicePicker g_devicePicker = nullptr;
HMENU g_trayMenu = nullptr;
std::unordered_map<std::wstring, std::pair<DeviceInformation, AudioPlaybackConnection>> g_audioPlaybackConnections;
std::unordered_map<std::wstring, Clock::time_point> g_lastCloseTime;
std::unordered_map<std::wstring, DuckedSessionInfo> g_duckedSessions;
std::vector<HANDLE> g_backendEventPipeClients;
std::mutex g_backendEventPipeClientsMutex;
std::thread g_backendCommandPipeThread;
std::thread g_backendEventPipeThread;
HANDLE g_backendPipeStopEvent = nullptr;
CheckBox g_autoStartCheckBox = nullptr;
ComboBox g_outputDeviceComboBox = nullptr;
Slider g_volumeSlider = nullptr;
TextBlock g_volumeValueText = nullptr;
CheckBox g_duckOtherAppsCheckBox = nullptr;
Slider g_duckedAppsVolumeSlider = nullptr;
TextBlock g_duckedAppsVolumeValueText = nullptr;
CheckBox g_showStartupToastCheckBox = nullptr;
HICON g_hIconLight = nullptr;
HICON g_hIconDark = nullptr;
NOTIFYICONDATAW g_nid = {
	.cbSize = sizeof(g_nid),
	.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP,
	.uCallbackMessage = WM_NOTIFYICON,
	.uVersion = NOTIFYICON_VERSION_4
};
NOTIFYICONIDENTIFIER g_niid = {
	.cbSize = sizeof(g_niid)
};
UINT WM_TASKBAR_CREATED = 0;
bool g_autoStart = false;
bool g_reconnect = false;
bool g_duckOtherApps = true;
bool g_showStartupToast = false;
double g_volume = 1.0;
double g_duckedAppsVolume = 0.35;
bool g_isRefreshingOutputDeviceComboBox = false;
bool g_isProgrammaticOutputDeviceSelection = false;
std::vector<std::wstring> g_lastDevices;
std::vector<OutputDeviceInfo> g_outputDevices;
std::wstring g_outputDeviceId;
std::wstring g_activeOutputDeviceId;
uint64_t g_outputDeviceSnapshotToken = 0;
uint64_t g_outputRoutingToken = 0;
bool g_outputSwitchSoftReconnectInProgress = false;
std::wstring g_startupBackendCommand;
std::wstring g_startupGuiCommand = L"show";

#include "Util.hpp"
#include "I18n.hpp"
#include "SettingsUtil.hpp"
#include "Direct2DSvg.hpp"
