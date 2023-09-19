#include <obs.hpp>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-config.h>

#include <Windows.h>
#include <stdio.h>
#include <string>
#include <filesystem>

#include "GrpcPlugin.h"
#include "PluginJsHandler.h"

PROCESS_INFORMATION g_browserProcessInfo;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("sl-browser-plugin", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return "Streamlabs OBS";
}

bool obs_module_load(void)
{
	AllocConsole();
	freopen("conin$", "r", stdin);
	freopen("conout$", "w", stdout);
	freopen("conout$", "w", stderr);
	printf("Plugin loaded\n");

	PluginJsHandler::instance().loadSlabsBrowserDocks();
	return true;
}

void obs_module_post_load(void)
{
	PluginJsHandler::instance().start();

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

	blog(LOG_INFO, "%s: Sending %d and %d to proxy\n", obs_module_description(), myListenPort, targetListenPort);

	STARTUPINFOW si;
	memset(&si, NULL, sizeof(si));
	si.cb = sizeof(si);

	if (GrpcPlugin::instance().startServer(myListenPort))
	{
		try
		{
			const char *module_path = obs_get_module_binary_path(obs_current_module());

			if (!module_path)
				return;

			std::wstring process_path = std::filesystem::u8path(module_path).remove_filename().wstring() + L"/sl-browser.exe";
			std::wstring startparams = L"sl-browser " + std::to_wstring(GetCurrentProcessId()) + L" " + std::to_wstring(myListenPort) + L" " + std::to_wstring(targetListenPort);
			browserGood = CreateProcessW(process_path.c_str(), (LPWSTR)startparams.c_str(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &g_browserProcessInfo);
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
	}

	std::string errorMsg = "Failed to initialize plugin " + std::string(obs_module_description()) + "\nRestart the application and try again.";

	if (!browserGood)
	{
		::MessageBoxA(NULL, errorMsg.c_str(), "Streamlabs Error", MB_ICONERROR | MB_TOPMOST);
		blog(LOG_ERROR, "Streamlabs: obs_module_post_load can't start proxy process, GetLastError = %d", GetLastError());
		return;
	}
}

void obs_module_unload(void)
{
	printf("obs_module_unload\n");

	// Tell process to shut down and wait?
	// Might be fine to just kill it, tbd
	// ;

	PluginJsHandler::instance().stop();
	GrpcPlugin::instance().stop();

	// Terminates the process (it shouldn't exist)
	TerminateProcess(g_browserProcessInfo.hProcess, EXIT_SUCCESS);
	CloseHandle(g_browserProcessInfo.hProcess);
	CloseHandle(g_browserProcessInfo.hThread);
}
