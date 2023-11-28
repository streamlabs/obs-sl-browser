#pragma once

#include <Dbghelp.h>
#include <filesystem>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <detours.h>
#include <ShlObj.h>
#include <psapi.h>

#include "WindowsFunctions.h"

#pragma comment(lib, "Psapi.lib")

class CrashHandler
{
public:
	void setUploadMsg(const std::string &msg) { m_uploadMessage = msg; }
	void setUploadTitle(const std::string &msg) { m_uploadTitle = msg; }
	void setErrorMsg(const std::string &msg) { m_errorMessage = msg; }
	void setErrorTitle(const std::string &msg) { m_errorTitle = msg; }
	void setNormalCrashHandler(std::function<void()> func) { m_normalCrashHandler = func; }
	void setSentryUri(const std::string &uri) { m_sentryURI = uri; }
	void setLogfilePath(const std::string &path) { m_logfilePath = path; }

public:
	static CrashHandler& instance()
	{
		static CrashHandler a;
		return a;
	}

private:
	CrashHandler()
	{
		::SetUnhandledExceptionFilter(unhandledHandler);
	}

	std::string m_errorTitle;
	std::string m_errorMessage;
	std::string m_uploadTitle;
	std::string m_uploadMessage;
	std::string m_sentryURI;
	std::string m_logfilePath;
	std::function<void()> m_normalCrashHandler;

private:
	static void makeMinidump(EXCEPTION_POINTERS *e)
	{
		auto hDbgHelp = LoadLibraryA("dbghelp");
	
		if (hDbgHelp == nullptr)
		    return;
	
		auto pMiniDumpWriteDump = (decltype(&MiniDumpWriteDump))GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
	
		if (pMiniDumpWriteDump == nullptr)
		    return;

		MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
		exceptionInfo.ThreadId = GetCurrentThreadId();
		exceptionInfo.ExceptionPointers = e;
		exceptionInfo.ClientPointers = FALSE;
	
		char moduleFileName[MAX_PATH];
		GetModuleFileNameA(GetModuleHandleA(0), moduleFileName, MAX_PATH);
		std::string exeName = std::filesystem::path(moduleFileName).stem().string();
	
		SYSTEMTIME st;
		GetSystemTime(&st);
	
		std::ostringstream oss;
		oss << std::setfill('0')
		    << std::setw(4) << st.wYear
		    << std::setw(2) << st.wMonth
		    << std::setw(2) << st.wDay << '_'
		    << std::setw(2) << st.wHour
		    << std::setw(2) << st.wMinute
		    << std::setw(2) << st.wSecond << ".dmp";	
	
		char appdatapath[MAX_PATH];
		std::string prefix = "Crashes\\";
		
		if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appdatapath)))
		    prefix = std::string(appdatapath) + "\\StreamlabsOBS\\Crashes\\";
		
		std::string timestamp = oss.str();
		std::string finalPath_FullDmp = prefix + exeName + timestamp;
		std::string finalPath_StackDmp = prefix + exeName + "_stack_" + timestamp;
	
		std::filesystem::create_directory(prefix);

		if (!instance().m_errorMessage.empty())
		    MessageBoxA(NULL, instance().m_errorMessage.c_str(), instance().m_errorTitle.c_str(), MB_OK | MB_ICONERROR | MB_TASKMODAL);

		HANDLE hFileStackDmp = CreateFileA(finalPath_StackDmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	
		if (hFileStackDmp != INVALID_HANDLE_VALUE)
		{
		    // Small dmp with just stack, in case heap is huge and something goes wrong with how easy it is to open
		    auto dumped = pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFileStackDmp, MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory), e ? &exceptionInfo : nullptr, nullptr, nullptr);
		    sendReportToSentry(instance().m_logfilePath, finalPath_StackDmp.c_str(), instance().m_sentryURI);
		    std::filesystem::remove(finalPath_StackDmp);
		    CloseHandle(hFileStackDmp);
		}

		// todo
		
		//int ret = MessageBoxA(NULL, instance().m_uploadMessage.c_str(), instance().m_uploadTitle.c_str(), MB_YESNO | MB_ICONQUESTION | MB_TASKMODAL);
		//
		//if (ret == IDYES)
		//{
		//    HANDLE hFileFullDmp = CreateFileA(finalPath_FullDmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
		//
		//    if (hFileFullDmp != INVALID_HANDLE_VALUE)
		//    {
		//	    // Full dmp with everything
		//	    auto dumped = pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFileFullDmp,
		//					     MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo | MiniDumpWithUnloadedModules | MiniDumpIgnoreInaccessibleMemory),
		//					     e ? &exceptionInfo : nullptr, nullptr, nullptr);
		//
		//	    CloseHandle(hFileFullDmp);
		//    }
		//}
	}

	static void sendReportToSentry(const std::string &logfile_path, const std::string &minidump_path, const std::string &uri)
	{
		DWORD httpCode = 0;
		DWORD timeoutMS = 10000;

		std::string method = "POST";
		std::string headers = "Content-Type: multipart/form-data; boundary=BOUNDARY\r\n";
		std::string response;
		std::string payload;
		std::string version;
		std::string githubRevision;

		#ifdef SL_OBS_VERSION
			version = SL_OBS_VERSION;
		#else
			return;
		#endif

		#ifdef GITHUB_REVISION
			githubRevision = GITHUB_REVISION;
		#else
			return;
		#endif

		// Read .dmp file content
		std::ifstream minidump_file(minidump_path, std::ios::binary);
		std::string minidumpContent((std::istreambuf_iterator<char>(minidump_file)), std::istreambuf_iterator<char>());

		// Open log file
		std::ifstream logfile(logfile_path, std::ios::binary | std::ios::ate); 
		std::streamsize size = logfile.tellg();                                
		logfile.seekg(0, std::ios::beg);                                       

		// If the log file is larger than 2MB, we want to keep only the last 2MB
		const std::streamsize maxSize = 2 * 1024 * 1024;
		if (size > maxSize)
		    logfile.seekg(-maxSize, std::ios::end);

		// Read the log file content into a string
		std::string logfileContent((std::istreambuf_iterator<char>(logfile)), std::istreambuf_iterator<char>());

		// Construct payload
		payload += "--BOUNDARY\r\n";
		payload += "Content-Disposition: form-data; name=\"upload_file_minidump\"; filename=\"mini.dmp\"\r\n";
		payload += "Content-Type: application/octet-stream\r\n";
		payload += "\r\n";
		payload += minidumpContent + "\r\n";
		payload += "--BOUNDARY\r\n";
		payload += "Content-Disposition: form-data; name=\"log_file\"; filename=\"log.txt\"\r\n";
		payload += "Content-Type: text/plain\r\n";
		payload += "\r\n";
		payload += logfileContent + "\r\n";
		payload += "--BOUNDARY\r\n";
		payload += "Content-Disposition: form-data; name=\"sentry\"\r\n";
		payload += "\r\n";
		payload += "{\"release\":\"" + version + "\", \"tags\":{\"gitrev\":\"" + githubRevision + "\"}}\r\n";
		payload += "--BOUNDARY--\r\n";

		// Ship it
		WindowsFunctions::HTTPRequest(uri, method, headers, &httpCode, timeoutMS, response, payload, "application/json");
	}

	EXCEPTION_POINTERS *m_exceptionPointers = nullptr;

	// Detour function
	static void MyExit(int status)
	{
		// OBS crash handler calls exit() at the end
		makeMinidump(instance().m_exceptionPointers);
		instance().m_exceptionPointers = nullptr;
	}

	static LONG CALLBACK unhandledHandler(EXCEPTION_POINTERS* e)
	{
		instance().m_exceptionPointers = e;

		// Override exit function
		if (instance().m_normalCrashHandler)
		{
		    HMODULE hModules[1024];
		    DWORD cbNeeded;
		    if (EnumProcessModules(GetCurrentProcess(), hModules, sizeof(hModules), &cbNeeded))
		    {
			    for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
			    {
				    char szModuleName[MAX_PATH];
				    if (GetModuleFileNameExA(GetCurrentProcess(), hModules[i], szModuleName, sizeof(szModuleName)))
				    {
					    if (strstr(szModuleName, "msvcrt.dll") || strstr(szModuleName, "ucrtbase.dll"))
					    {
						    FARPROC pExit = GetProcAddress(hModules[i], "exit");

						    if (pExit != NULL)
						    {
							    // Hook the exit function in the main module
							    DetourTransactionBegin();
							    DetourUpdateThread(GetCurrentThread());
							    DetourAttach((void **)&pExit, (void *)MyExit);
							    DetourTransactionCommit();
						    }
					    }
				    }
			    }
		    }

		    instance().m_normalCrashHandler();
		}

		// Run ours if haven't yet
		if (instance().m_exceptionPointers != nullptr)
			makeMinidump(e);

		return EXCEPTION_CONTINUE_SEARCH;
	}
};
