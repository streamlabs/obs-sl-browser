#include <obs.hpp>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-config.h>
#include <util\platform.h>

#include <Windows.h>
#include <stdio.h>
#include <string>
#include <filesystem>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "GrpcPlugin.h"
#include "PluginJsHandler.h"
#include "WebServer.h"
#include "ConsoleToggle.h"
#include "CrashHandler.h"
#include "QtGuiModifications.h"

#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolbar>

PROCESS_INFORMATION g_browserProcessInfo;
static std::thread g_browserWatchThread;
static std::atomic<bool> g_browserTerminating = false;

static bool get_token_user(HANDLE token, std::vector<BYTE> &buffer)
{
	DWORD required = 0;
	if (GetTokenInformation(token, TokenUser, nullptr, 0, &required))
	{
		SetLastError(ERROR_INVALID_DATA);
		return false;
	}
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !required)
		return false;

	buffer.resize(required);
	return GetTokenInformation(token, TokenUser, buffer.data(), required, &required);
}

using unique_handle = std::unique_ptr<void, decltype(&CloseHandle)>;

static bool launch_browser_process(const std::wstring &process_path, const std::wstring &command_line, const std::wstring &working_dir, STARTUPINFOW &startup_info, DWORD &launch_error)
{
	const auto launch_with_obs_token = [&]() {
		std::wstring mutable_command_line = command_line;
		const BOOL started = CreateProcessW(process_path.c_str(), mutable_command_line.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr, working_dir.c_str(), &startup_info, &g_browserProcessInfo);
		launch_error = started ? ERROR_SUCCESS : GetLastError();
		return !!started;
	};
	const auto fallback = [&](const char *step) {
		const DWORD error = GetLastError();
		blog(LOG_WARNING, "[SL_PLUGIN]: %s failed with error %lu; trying OBS token", step, error);
		return launch_with_obs_token();
	};

	HANDLE raw_process_token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_process_token))
		return fallback("OpenProcessToken");
	unique_handle process_token(raw_process_token, CloseHandle);

	TOKEN_ELEVATION elevation{};
	DWORD returned = 0;
	if (!GetTokenInformation(process_token.get(), TokenElevation, &elevation, sizeof(elevation), &returned))
		return fallback("Query OBS elevation");
	if (!elevation.TokenIsElevated)
		return launch_with_obs_token();

	DWORD shell_pid = 0;
	const HWND shell_window = GetShellWindow();
	if (!shell_window || !GetWindowThreadProcessId(shell_window, &shell_pid) || !shell_pid)
	{
		SetLastError(ERROR_NOT_FOUND);
		return fallback("Find desktop shell");
	}

	HANDLE raw_shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
	if (!raw_shell_process)
		return fallback("Open desktop shell");
	unique_handle shell_process(raw_shell_process, CloseHandle);

	HANDLE raw_shell_token = nullptr;
	if (!OpenProcessToken(shell_process.get(), TOKEN_QUERY | TOKEN_DUPLICATE, &raw_shell_token))
		return fallback("Open desktop shell token");
	unique_handle shell_token(raw_shell_token, CloseHandle);

	std::vector<BYTE> obs_user;
	std::vector<BYTE> shell_user;
	if (!get_token_user(process_token.get(), obs_user) || !get_token_user(shell_token.get(), shell_user))
		return fallback("Query token users");
	const auto *obs_token_user = reinterpret_cast<const TOKEN_USER *>(obs_user.data());
	const auto *shell_token_user = reinterpret_cast<const TOKEN_USER *>(shell_user.data());
	if (!EqualSid(obs_token_user->User.Sid, shell_token_user->User.Sid))
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return fallback("Match desktop shell user");
	}

	TOKEN_ELEVATION shell_elevation{};
	if (!GetTokenInformation(shell_token.get(), TokenElevation, &shell_elevation, sizeof(shell_elevation), &returned))
		return fallback("Query desktop shell elevation");
	if (shell_elevation.TokenIsElevated)
	{
		SetLastError(ERROR_ACCESS_DENIED);
		return fallback("Require standard desktop shell token");
	}

	HANDLE raw_browser_token = nullptr;
	// Match Chromium's access mask for a medium primary token used by CreateProcessWithTokenW.
	if (!DuplicateTokenEx(shell_token.get(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, nullptr, SecurityImpersonation, TokenPrimary, &raw_browser_token))
		return fallback("Duplicate desktop shell token");
	unique_handle browser_token(raw_browser_token, CloseHandle);

	blog(LOG_INFO, "[SL_PLUGIN]: Launching browser with the desktop shell's standard user token");
	// The desktop shell already loaded this user's profile. LOGON_WITH_PROFILE
	// requires additional token rights and backup/restore privileges.
	// A null environment inherits OBS's process variables, including browser overrides.
	std::wstring mutable_command_line = command_line;
	if (CreateProcessWithTokenW(browser_token.get(), 0, process_path.c_str(), mutable_command_line.data(), CREATE_NEW_CONSOLE, nullptr, working_dir.c_str(), &startup_info, &g_browserProcessInfo))
	{
		launch_error = ERROR_SUCCESS;
		return true;
	}
	return fallback("CreateProcessWithTokenW");
}

OBS_DECLARE_MODULE()
MODULE_EXPORT const char *obs_module_description(void)
{
	return "Streamlabs";
}

bool obs_module_load(void)
{
#ifdef GITHUB_REVISION
	blog(LOG_INFO, "%s module git sha is %s for OBS %s, revision %s", obs_module_description(), std::string(GITHUB_REVISION).c_str(), std::string(SL_OBS_VERSION).c_str(), std::string(SL_REVISION).c_str());
#else
	blog(LOG_INFO, "%s module is a debug build, version information unknown.", obs_module_description());
#endif

	PluginJsHandler::instance().loadFonts();
	PluginJsHandler::instance().loadSlabsBrowserDocks();
	return true;
}

void obs_module_post_load(void)
{
	/***
	* Initializing crash handler information
	*/

	blog(LOG_INFO, "Starting %s plugin...", obs_module_description());
			
	CrashHandler::instance().setUploadTitle("OBS has crashed!");
	CrashHandler::instance().setUploadMsg("Would you like to upload a detailed report to Streamlabs for further review?");
	CrashHandler::instance().setSentryUri("https://o114354.ingest.sentry.io/api/4506154577756160/minidump/?sentry_key=9860cbedfdebf06a6f209d004b921add");
	
	// Path to OBS log file, find the most recently updated log file
	std::string logdir = os_get_config_path_ptr("obs-studio/logs/");
	std::filesystem::path latest_file;
	auto latest_time = std::filesystem::file_time_type::min();

	try
	{
		if (!std::filesystem::exists(logdir))
			throw std::runtime_error("Log directory does not exist.");
		if (!std::filesystem::is_directory(logdir))
			throw std::runtime_error("Specified path is not a directory.");

		std::filesystem::directory_iterator dir_it(logdir);

		// Find the most recently updated file
		for (const auto &entry : dir_it)
		{
			if (entry.is_regular_file())
			{
				auto last_write_time = entry.last_write_time();
				if (latest_file.empty() || last_write_time > latest_time)
				{
					latest_file = entry.path();
					latest_time = last_write_time;
				}
			}
		}
	}
	catch (const std::filesystem::filesystem_error &e)
	{
		blog(LOG_ERROR, "obs-studio/logs/ - Filesystem error: ", e.what());
	}
	catch (const std::runtime_error &e)
	{
		blog(LOG_ERROR, "obs-studio/logs/ - Runtime error: ", e.what());
	}
	catch (...)
	{
		blog(LOG_ERROR, "obs-studio/logs/ - An unknown error occurred.");
	}

	
	CrashHandler::instance().addLogfilePath(latest_file.string());
		
	// Path to CEF log file, find the most recently updated log file
	char cache_path[MAX_PATH];
	std::string cache_pathStdStr;
	
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, cache_path)))
		cache_pathStdStr = std::string(cache_path) + "\\StreamlabsOBS_CEF_Cache";
	
	if (!cache_pathStdStr.empty())
		CrashHandler::instance().addLogfilePath(cache_pathStdStr + "\\cef.log");

	/***
	* Plugin begnis
	*/

	QtGuiModifications::instance();
	PluginJsHandler::instance().start();

	obs_frontend_add_event_callback(PluginJsHandler::instance().handle_obs_frontend_event, nullptr);
	obs_frontend_add_event_callback(QtGuiModifications::instance().handle_obs_frontend_event, nullptr);

	auto chooseProxyPort = []() {
		int32_t result = 0;
		struct sockaddr_in local;
		SOCKET sockt = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);

		if (sockt == INVALID_SOCKET)
			return result;

		local.sin_addr.s_addr = htonl(INADDR_ANY);
		local.sin_family = AF_INET;
		local.sin_port = htons(0);

		// Bind
		if (::bind(sockt, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR)
			return result;

		struct sockaddr_in my_addr;
		memset(&my_addr, NULL, sizeof(my_addr));
		int len = sizeof(my_addr);
		getsockname(sockt, (struct sockaddr *)&my_addr, &len);
		result = ntohs(my_addr.sin_port);

		closesocket(sockt);
		return result;
	};

	bool browserGood = false;
	int32_t myListenPort = chooseProxyPort();
	int32_t targetListenPort = chooseProxyPort();
 
	blog(LOG_INFO, "%s: Sending %d and %d to proxy", obs_module_description(), myListenPort, targetListenPort);

	STARTUPINFOW si;
	memset(&si, NULL, sizeof(si));
	si.cb = sizeof(si);

	if (GrpcPlugin::instance().startServer(myListenPort))
	{
		try
		{
			char absolute_path[MAX_PATH];
			GetFullPathNameA(obs_get_module_binary_path(obs_current_module()), MAX_PATH, absolute_path, NULL);

			if (!absolute_path)
				return;
			blog(LOG_ERROR, "[SL_PLUGIN]: obs_module_post_load Module path: %s", absolute_path);
			std::filesystem::path process_dir = std::filesystem::u8path(absolute_path).parent_path();
			if (obs_module_t *browser_module = obs_get_module("obs-browser"))
			{
				if (const char *browser_path = obs_get_module_binary_path(browser_module))
				{
					const auto browser_dir = std::filesystem::u8path(browser_path).parent_path();
					if (std::filesystem::exists(browser_dir / "sl-browser.exe"))
						process_dir = browser_dir;
				}
			}
			std::wstring process_path = (process_dir / "sl-browser.exe").wstring();
			std::wstring obs_exe_path(MAX_PATH, L'\0');
			DWORD obs_exe_path_length = 0;
			for (;;)
			{
				obs_exe_path_length = GetModuleFileNameW(nullptr, obs_exe_path.data(), static_cast<DWORD>(obs_exe_path.size()));
				if (obs_exe_path_length == 0 || obs_exe_path_length < obs_exe_path.size())
					break;
				if (obs_exe_path.size() >= 32768)
				{
					SetLastError(ERROR_INSUFFICIENT_BUFFER);
					obs_exe_path_length = 0;
					break;
				}
				obs_exe_path.resize(obs_exe_path.size() * 2);
			}
			if (obs_exe_path_length == 0)
			{
				const DWORD path_error = GetLastError();
				blog(LOG_ERROR, "[SL_PLUGIN]: Could not determine the OBS executable directory, error %lu", path_error);
				SetLastError(path_error);
			}
			else
			{
				obs_exe_path.resize(obs_exe_path_length);
				const std::wstring obs_binary_dir = std::filesystem::path(obs_exe_path).parent_path().wstring();
				std::wstring startparams = L"sl-browser " + std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(myListenPort) + L" " + std::to_wstring(targetListenPort);
				blog(LOG_ERROR, "[SL_PLUGIN]: obs_module_post_load process_path: %ls", process_path.c_str());
				blog(LOG_ERROR, "[SL_PLUGIN]: obs_module_post_load startparams: %ls", startparams.c_str());
				DWORD launch_error = ERROR_SUCCESS;
				browserGood = launch_browser_process(process_path, startparams, obs_binary_dir, si, launch_error);
				if (!browserGood)
				{
					blog(LOG_ERROR, "[SL_PLUGIN]: Browser process launch failed with error %lu", launch_error);
					SetLastError(launch_error);
				}
			}
		}
		catch (...)
		{
			blog(LOG_ERROR, "%s: obs_module_post_load catch while launching server", obs_module_description());
		}

		if (browserGood && !GrpcPlugin::instance().connectToClient(targetListenPort))
		{
			browserGood = FALSE;
			blog(LOG_ERROR, "%s: obs_module_post_load can't connect to process, GetLastError = %d", obs_module_description(), GetLastError());

			// Terminates the process
			TerminateProcess(g_browserProcessInfo.hProcess, EXIT_SUCCESS);
			CloseHandle(g_browserProcessInfo.hProcess);
			CloseHandle(g_browserProcessInfo.hThread);
		}
		blog(LOG_ERROR, "[SL_PLUGIN]: obs_module_post_load exit startServer");
	} else {
		blog(LOG_ERROR, "[SL_PLUGIN]: obs_module_post_load failed to start server");
	}

	std::string errorMsg = "Failed to initialize plugin " + std::string(obs_module_description()) + "\nRestart the application and try again.";

	if (!browserGood)
	{
		::MessageBoxA(NULL, errorMsg.c_str(), "Streamlabs Error", MB_ICONERROR | MB_TOPMOST);
		blog(LOG_ERROR, "Streamlabs: obs_module_post_load can't start proxy process, GetLastError = %d", GetLastError());
		return;
	}

	g_browserWatchThread = std::thread([]() {
		WaitForSingleObject(g_browserProcessInfo.hProcess, INFINITE);

		DWORD exitCode = 0;
		GetExitCodeProcess(g_browserProcessInfo.hProcess, &exitCode);
		if (!g_browserTerminating)
			blog(LOG_ERROR, "[SL_PLUGIN]: sl-browser process exited unexpectedly, exit code = %lu (1000 + N = CefInitialize failed with CEF exit code N)", exitCode);
	});

	// Streamlabs button to toggle visibility
	QMainWindow * window = (QMainWindow *)obs_frontend_get_main_window();
	QAction *action = new QAction("Streamlabs", window);
	QObject::connect(action, &QAction::triggered, [=]() {

		// Has to be run in a seperate thread because bringing to foreground will take over input msg and we're in the middle of using it
		std::thread([&]() {
			if (auto *client = GrpcPlugin::instance().getClient())
				client->send_windowToggleVisibility();
		}).detach();
	});

	window->menuBar()->addAction(action);
}

void obs_module_unload(void)
{
	// Terminates the browser process (it shouldn't exist)
	g_browserTerminating = true;
	::TerminateProcess(g_browserProcessInfo.hProcess, EXIT_SUCCESS);
	if (g_browserWatchThread.joinable())
		g_browserWatchThread.join();
	::CloseHandle(g_browserProcessInfo.hProcess);
	::CloseHandle(g_browserProcessInfo.hThread);

	// JS handler needs to be stopped before Grpc or crash
	PluginJsHandler::instance().stop();
	GrpcPlugin::instance().stop();
	WebServer::instance().stop();
}
