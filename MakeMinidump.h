#ifndef _MAKEMINIDUMP_H
#define _MAKEMINIDUMP_H

#include <Dbghelp.h>
#include <filesystem>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace Util
{
	// google code
    void makeMinidump(EXCEPTION_POINTERS* e)
    {
        auto hDbgHelp = LoadLibraryA("dbghelp");

        if (hDbgHelp == nullptr)
            return;

        auto pMiniDumpWriteDump = (decltype(&MiniDumpWriteDump))GetProcAddress(hDbgHelp, "MiniDumpWriteDump");

        if (pMiniDumpWriteDump == nullptr)
            return;

        Sleep(1000);

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

        std::string timestamp = oss.str();
        std::string finalPath_FullDmp = "Crashes\\" + exeName + timestamp;
        std::string finalPath_StackDmp = "Crashes\\" + exeName + "_stack_" + timestamp;

        std::filesystem::create_directory("Crashes");

        HANDLE hFileStackDmp = CreateFileA(finalPath_StackDmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
        HANDLE hFileFullDmp = CreateFileA(finalPath_FullDmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

        if (hFileFullDmp == INVALID_HANDLE_VALUE || hFileStackDmp == INVALID_HANDLE_VALUE)
            return;

        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
        exceptionInfo.ThreadId = GetCurrentThreadId();
        exceptionInfo.ExceptionPointers = e;
        exceptionInfo.ClientPointers = FALSE;

        // Small dmp with just stack, in case heap is huge and something goes wrong with how easy it is to open
        auto dumped = pMiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFileStackDmp,
            MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
            e ? &exceptionInfo : nullptr,
            nullptr,
            nullptr);

        // Full dmp with everything
        dumped = pMiniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            hFileFullDmp,
            MINIDUMP_TYPE(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithProcessThreadData |
                MiniDumpWithFullMemoryInfo | MiniDumpWithUnloadedModules | MiniDumpIgnoreInaccessibleMemory),
            e ? &exceptionInfo : nullptr,
            nullptr,
            nullptr);

        CloseHandle(hFileStackDmp);
        CloseHandle(hFileFullDmp);
        return;
    }


	LONG CALLBACK unhandledHandler(EXCEPTION_POINTERS* e)
	{
		makeMinidump(e);
		return EXCEPTION_CONTINUE_SEARCH;
	}
}

#endif
