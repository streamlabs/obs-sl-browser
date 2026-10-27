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

static bool launch_browser_process(const std::wstring &process_path, std::wstring &command_line,
				   const std::wstring &working_dir, STARTUPINFOW &startup_info)
{
#ifdef SL_BROWSER_E2E_USE_OBS_TOKEN
	// The CI runner has no standard desktop shell token. Keep its existing
	// launch behavior so the suites can exercise the rest of the plugin.
	return CreateProcessW(process_path.c_str(), command_line.data(), nullptr, nullptr, FALSE,
			      CREATE_NEW_CONSOLE, nullptr, working_dir.c_str(), &startup_info, &g_browserProcessInfo);
#endif

	HANDLE process_token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &process_token))
	{
		const DWORD token_error = GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: OpenProcessToken failed with error %lu", token_error);
		SetLastError(token_error);
		return false;
	}

	TOKEN_ELEVATION elevation{};
	DWORD returned = 0;
	if (!GetTokenInformation(process_token, TokenElevation, &elevation, sizeof(elevation), &returned))
	{
		const DWORD query_error = GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not query OBS elevation, error %lu", query_error);
		CloseHandle(process_token);
		SetLastError(query_error);
		return false;
	}

	if (!elevation.TokenIsElevated)
	{
		CloseHandle(process_token);
		return CreateProcessW(process_path.c_str(), command_line.data(), nullptr, nullptr, FALSE,
				      CREATE_NEW_CONSOLE, nullptr, working_dir.c_str(), &startup_info, &g_browserProcessInfo);
	}

	std::vector<BYTE> obs_user;
	if (!get_token_user(process_token, obs_user))
	{
		const DWORD token_error = GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not query OBS's user, error %lu", token_error);
		CloseHandle(process_token);
		SetLastError(token_error);
		return false;
	}
	CloseHandle(process_token);

	// UAC's linked token may only be an identification token. Use the desktop shell's
	// primary token to start the Qt-dependent helper at standard integrity.
	DWORD shell_pid = 0;
	const HWND shell_window = GetShellWindow();
	if (!shell_window || !GetWindowThreadProcessId(shell_window, &shell_pid) || !shell_pid)
	{
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not find the desktop shell process");
		SetLastError(ERROR_NOT_FOUND);
		return false;
	}

	DWORD obs_session = 0;
	DWORD shell_session = 0;
	if (!ProcessIdToSessionId(GetCurrentProcessId(), &obs_session) ||
	    !ProcessIdToSessionId(shell_pid, &shell_session) || obs_session != shell_session)
	{
		const DWORD session_error = obs_session != shell_session ? ERROR_ACCESS_DENIED : GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Desktop shell is not in OBS's session, error %lu", session_error);
		SetLastError(session_error);
		return false;
	}

	HANDLE shell_process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shell_pid);
	if (!shell_process)
	{
		const DWORD process_error = GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not open the desktop shell process, error %lu", process_error);
		SetLastError(process_error);
		return false;
	}

	HANDLE shell_token = nullptr;
	const BOOL opened_shell_token = OpenProcessToken(shell_process, TOKEN_QUERY | TOKEN_DUPLICATE, &shell_token);
	const DWORD shell_token_error = opened_shell_token ? ERROR_SUCCESS : GetLastError();
	CloseHandle(shell_process);
	if (!opened_shell_token)
	{
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not open the desktop shell token, error %lu", shell_token_error);
		SetLastError(shell_token_error);
		return false;
	}

	std::vector<BYTE> shell_user;
	if (!get_token_user(shell_token, shell_user))
	{
		const DWORD token_error = GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not query the desktop shell's user, error %lu", token_error);
		CloseHandle(shell_token);
		SetLastError(token_error);
		return false;
	}
	const auto *obs_token_user = reinterpret_cast<const TOKEN_USER *>(obs_user.data());
	const auto *shell_token_user = reinterpret_cast<const TOKEN_USER *>(shell_user.data());
	if (!EqualSid(obs_token_user->User.Sid, shell_token_user->User.Sid))
	{
		blog(LOG_ERROR, "[SL_PLUGIN]: Desktop shell belongs to a different user than OBS");
		CloseHandle(shell_token);
		SetLastError(ERROR_ACCESS_DENIED);
		return false;
	}

	TOKEN_ELEVATION shell_elevation{};
	if (!GetTokenInformation(shell_token, TokenElevation, &shell_elevation,
				 sizeof(shell_elevation), &returned) || shell_elevation.TokenIsElevated)
	{
		const DWORD token_error = shell_elevation.TokenIsElevated ? ERROR_ACCESS_DENIED : GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Desktop shell token is not a standard user token, error %lu", token_error);
		CloseHandle(shell_token);
		SetLastError(token_error);
		return false;
	}

	HANDLE browser_token = nullptr;
	if (!DuplicateTokenEx(shell_token, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY,
			      nullptr, SecurityImpersonation, TokenPrimary, &browser_token))
	{
		const DWORD token_error = GetLastError();
		blog(LOG_ERROR, "[SL_PLUGIN]: Could not duplicate the desktop shell token, error %lu", token_error);
		CloseHandle(shell_token);
		SetLastError(token_error);
		return false;
	}
	CloseHandle(shell_token);

	blog(LOG_INFO, "[SL_PLUGIN]: Launching browser with the desktop shell's standard user token");
	// The desktop shell already loaded this user's profile. LOGON_WITH_PROFILE
	// requires additional token rights and backup/restore privileges.
	const BOOL started = CreateProcessWithTokenW(browser_token, 0,
						     process_path.c_str(), command_line.data(), CREATE_NEW_CONSOLE,
						     nullptr, working_dir.c_str(), &startup_info, &g_browserProcessInfo);
	const DWORD launch_error = started ? ERROR_SUCCESS : GetLastError();
	CloseHandle(browser_token);
	SetLastError(launch_error);
	return started;
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
				browserGood = launch_browser_process(process_path, startparams, obs_binary_dir, si);
				if (!browserGood)
					blog(LOG_ERROR, "[SL_PLUGIN]: Browser process launch failed with error %lu", GetLastError());
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
