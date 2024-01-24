
#include "cef-headers.hpp"
#include "BrowserApp.h"

#include <windows.h>
#include <string>
#include <thread>

// GPU hint exports for AMD/NVIDIA laptops
#ifdef _MSC_VER
extern "C" __declspec(dllexport) DWORD NvOptimusEnablement = 1;
extern "C" __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

static HANDLE shutdown_event = nullptr;
static bool thread_initialized = false;

DECLARE_HANDLE(OBS_DPI_AWARENESS_CONTEXT);
#define OBS_DPI_AWARENESS_CONTEXT_UNAWARE ((OBS_DPI_AWARENESS_CONTEXT)-1)
#define OBS_DPI_AWARENESS_CONTEXT_SYSTEM_AWARE ((OBS_DPI_AWARENESS_CONTEXT)-2)
#define OBS_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE ((OBS_DPI_AWARENESS_CONTEXT)-3)
#define OBS_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((OBS_DPI_AWARENESS_CONTEXT)-4)

static bool SetHighDPIv2Scaling()
{
	static BOOL(WINAPI * func)(OBS_DPI_AWARENESS_CONTEXT) = nullptr;
	func = reinterpret_cast<decltype(func)>(GetProcAddress(GetModuleHandleW(L"USER32"), "SetProcessDpiAwarenessContext"));
	if (!func)
	{
		return false;
	}

	return !!func(OBS_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

static void shutdown_check_thread(DWORD parent_pid, DWORD main_thread_id)
{
	HANDLE parent = OpenProcess(SYNCHRONIZE, false, parent_pid);
	if (!parent)
	{
		return;
	}

	HANDLE handles[2] = {parent, shutdown_event};

	DWORD ret = WaitForMultipleObjects(2, handles, false, INFINITE);
	if (ret == WAIT_OBJECT_0)
	{
		PostThreadMessage(main_thread_id, WM_QUIT, 0, 0);
		ret = WaitForSingleObject(shutdown_event, 5000);
		if (ret != WAIT_OBJECT_0)
		{
			TerminateProcess(GetCurrentProcess(), (UINT)-1);
		}
	}

	CloseHandle(parent);
}

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	std::thread shutdown_check;

	CefMainArgs mainArgs(nullptr);
	if (!SetHighDPIv2Scaling())
		CefEnableHighDPISupport();

	CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
	command_line->InitFromString(::GetCommandLineW());

	std::string parent_pid_str = command_line->GetSwitchValue("parent_pid");
	std::string current_pid = std::to_string(GetCurrentProcessId());
	if (!parent_pid_str.empty())
	{
		shutdown_event = CreateEvent(nullptr, true, false, nullptr);
		DWORD parent_pid = (DWORD)std::stoi(parent_pid_str);
		shutdown_check = std::thread(shutdown_check_thread, parent_pid, GetCurrentThreadId());
		thread_initialized = true;
	}

	int ret = CefExecuteProcess(mainArgs, &BrowserApp::instance(), NULL);

	/* chromium browser subprocesses actually have TerminateProcess called
	 * on them for whatever reason, so it's unlikely this code will ever
	 * get called, but better to be safe than sorry */
	if (thread_initialized)
	{
		SetEvent(shutdown_event);
		shutdown_check.join();
	}

	if (shutdown_event)
		CloseHandle(shutdown_event);

	return ret;
}
