#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>
#include <wininet.h>
#include <fstream>
#include <TlHelp32.h>

#include "deps/minizip/unzip.h"

#pragma comment(lib, "wininet.lib")

namespace WindowsFunctions
{
	// Test only: windows are kept off the desktop and never take focus
	static bool TestOffscreen()
	{
		static const bool on = GetEnvironmentVariableA("SL_PLUGIN_TEST_OFFSCREEN", nullptr, 0) > 0;
		return on;
	}

	static void ForceForegroundWindow(HWND focusOnWindowHandle)
	{
		if (TestOffscreen())
		{
			ShowWindow(focusOnWindowHandle, SW_SHOWNOACTIVATE);
			return;
		}

		int style = GetWindowLong(focusOnWindowHandle, GWL_STYLE);

		// Minimize and restore to be able to make it active.
		if ((style & WS_MINIMIZE) == WS_MINIMIZE)
			ShowWindow(focusOnWindowHandle, SW_RESTORE);

		auto currentlyFocusedWindowProcessId = GetWindowThreadProcessId(GetForegroundWindow(), LPDWORD(0));
		auto appThread = GetCurrentThreadId();

		if (currentlyFocusedWindowProcessId != appThread)
		{
			AttachThreadInput(currentlyFocusedWindowProcessId, appThread, true);
			BringWindowToTop(focusOnWindowHandle);
			AttachThreadInput(currentlyFocusedWindowProcessId, appThread, false);
		}

		else
		{
			BringWindowToTop(focusOnWindowHandle);
		}
	}

	static bool IsUnsafeZipComponent(const std::wstring &component, const char *&out_reason)
	{
		for (wchar_t c : component)
		{
			if (c < 0x20 || wcschr(L":<>\"|?*", c))
			{
				out_reason = "invalid character";
				return true;
			}
		}

		if (component.find_last_not_of(L". ") == std::wstring::npos)
		{
			out_reason = "dot-only name";
			return true;
		}

		std::wstring base = component.substr(0, component.find(L'.'));
		base.erase(base.find_last_not_of(L' ') + 1);
		for (wchar_t &c : base)
			c = (wchar_t)::towupper(c);

		static const wchar_t *const devices[] = {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"};
		for (const wchar_t *device : devices)
		{
			if (base == device)
			{
				out_reason = "reserved device name";
				return true;
			}
		}

		// COM1-9 and LPT1-9, including the superscript digits Windows also reserves.
		if (base.size() == 4 && (base.compare(0, 3, L"COM") == 0 || base.compare(0, 3, L"LPT") == 0))
		{
			wchar_t digit = base[3];
			if ((digit >= L'1' && digit <= L'9') || digit == 0x00B9 || digit == 0x00B2 || digit == 0x00B3)
			{
				out_reason = "reserved device name";
				return true;
			}
		}

		return false;
	}

	static std::wstring ToLowerWide(std::wstring s)
	{
		for (wchar_t &c : s)
			c = (wchar_t)::towlower(c);
		return s;
	}

	struct ZipEntryTarget
	{
		std::filesystem::path path;
		bool isDir = false;
		bool skip = false;
	};

	static bool ResolveZipEntry(const std::vector<char> &rawName, bool isUtf8, const std::filesystem::path &destDir, ZipEntryTarget &out_target, const char *&out_reason)
	{
		if (rawName.empty() || memchr(rawName.data(), 0, rawName.size()) != nullptr)
		{
			out_reason = "empty name or embedded NUL";
			return false;
		}

		const UINT codePage = isUtf8 ? CP_UTF8 : CP_ACP;
		const DWORD flags = isUtf8 ? MB_ERR_INVALID_CHARS : 0;
		const int needed = MultiByteToWideChar(codePage, flags, rawName.data(), (int)rawName.size(), nullptr, 0);
		if (needed <= 0)
		{
			out_reason = "undecodable name";
			return false;
		}

		std::wstring name((size_t)needed, L'\0');
		MultiByteToWideChar(codePage, flags, rawName.data(), (int)rawName.size(), name.data(), needed);

		if (name.front() == L'/' || name.front() == L'\\')
		{
			out_reason = "rooted path";
			return false;
		}

		out_target.isDir = (name.back() == L'/' || name.back() == L'\\');

		std::filesystem::path relative;
		size_t start = 0;
		while (start <= name.size())
		{
			size_t end = name.find_first_of(L"/\\", start);
			if (end == std::wstring::npos)
				end = name.size();

			std::wstring component = name.substr(start, end - start);
			start = end + 1;

			if (component.empty() || component == L".")
				continue;

			if (component == L"..")
			{
				out_reason = "parent directory reference";
				return false;
			}

			if (IsUnsafeZipComponent(component, out_reason))
				return false;

			relative /= component;
		}

		if (relative.empty())
		{
			if (!out_target.isDir)
			{
				out_reason = "no file name";
				return false;
			}

			// A directory entry naming the destination itself, such as "./"
			out_target.skip = true;
			return true;
		}

		std::filesystem::path root = destDir.lexically_normal();
		std::filesystem::path full = (root / relative).lexically_normal();

		std::wstring rootStr = ToLowerWide(root.wstring());
		while (!rootStr.empty() && (rootStr.back() == L'\\' || rootStr.back() == L'/'))
			rootStr.pop_back();

		const std::wstring fullStr = ToLowerWide(full.wstring());
		const bool contained = fullStr.size() > rootStr.size() && fullStr.compare(0, rootStr.size(), rootStr) == 0 && (fullStr[rootStr.size()] == L'\\' || fullStr[rootStr.size()] == L'/');
		if (!contained)
		{
			out_reason = "escapes the destination folder";
			return false;
		}

		out_target.path = full;
		return true;
	}

	// Validates every entry before writing any of them, so a rejected archive extracts nothing.
	static bool Unzip(const std::string &zipPathUtf8, const std::filesystem::path &destDir, std::vector<std::filesystem::path> &out_files, std::string &out_error)
	{
		unzFile zipFile = unzOpen(zipPathUtf8.c_str());
		if (!zipFile)
		{
			out_error = "Could not open zip";
			return false;
		}

		unz_global_info globalInfo;
		if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
		{
			unzClose(zipFile);
			out_error = "Could not read zip directory";
			return false;
		}

		std::vector<ZipEntryTarget> targets;

		for (uLong i = 0; i < globalInfo.number_entry; ++i)
		{
			unz_file_info fileInfo;
			if (unzGetCurrentFileInfo(zipFile, &fileInfo, NULL, 0, NULL, 0, NULL, 0) != UNZ_OK)
			{
				unzClose(zipFile);
				out_error = "Could not read zip entry #" + std::to_string(i);
				return false;
			}

			std::vector<char> rawName(fileInfo.size_filename + 1);
			if (unzGetCurrentFileInfo(zipFile, &fileInfo, rawName.data(), (uLong)rawName.size(), NULL, 0, NULL, 0) != UNZ_OK)
			{
				unzClose(zipFile);
				out_error = "Could not read zip entry #" + std::to_string(i);
				return false;
			}
			rawName.pop_back();

			ZipEntryTarget target;
			const char *reason = "";
			if (!ResolveZipEntry(rawName, (fileInfo.flag & 0x800) != 0, destDir, target, reason))
			{
				unzClose(zipFile);
				out_error = "Unsafe zip entry #" + std::to_string(i) + ": " + reason;
				return false;
			}
			targets.push_back(std::move(target));

			if ((i + 1) < globalInfo.number_entry && unzGoToNextFile(zipFile) != UNZ_OK)
			{
				unzClose(zipFile);
				out_error = "Could not read zip entry #" + std::to_string(i + 1);
				return false;
			}
		}

		if (unzGoToFirstFile(zipFile) != UNZ_OK && globalInfo.number_entry > 0)
		{
			unzClose(zipFile);
			out_error = "Could not read zip directory";
			return false;
		}

		char buffer[4096];

		for (uLong i = 0; i < globalInfo.number_entry; ++i)
		{
			const ZipEntryTarget &target = targets[i];
			std::error_code ec;

			if (target.skip)
			{
				// Nothing to create
			}
			else if (target.isDir)
			{
				std::filesystem::create_directories(target.path, ec);
				if (ec)
				{
					unzClose(zipFile);
					out_error = "Could not create folder for entry #" + std::to_string(i);
					return false;
				}
			}
			else
			{
				if (unzOpenCurrentFile(zipFile) != UNZ_OK)
				{
					unzClose(zipFile);
					out_error = "Could not open zip entry #" + std::to_string(i);
					return false;
				}

				std::filesystem::create_directories(target.path.parent_path(), ec);
				std::ofstream outFile;
				if (!ec)
					outFile.open(target.path, std::ios::binary);

				if (ec || !outFile.is_open())
				{
					unzCloseCurrentFile(zipFile);
					unzClose(zipFile);
					out_error = "Could not create file for entry #" + std::to_string(i);
					return false;
				}

				int error = UNZ_OK;
				do
				{
					error = unzReadCurrentFile(zipFile, buffer, sizeof(buffer));
					if (error < 0)
					{
						unzCloseCurrentFile(zipFile);
						unzClose(zipFile);
						out_error = "Could not read zip entry #" + std::to_string(i);
						return false;
					}
					if (error > 0)
					{
						outFile.write(buffer, error);
					}
				} while (error > 0);

				outFile.close();
				if (outFile.fail())
				{
					unzCloseCurrentFile(zipFile);
					unzClose(zipFile);
					out_error = "Could not write file for entry #" + std::to_string(i);
					return false;
				}

				out_files.push_back(target.path);

				if (unzCloseCurrentFile(zipFile) != UNZ_OK)
				{
					unzClose(zipFile);
					out_error = "Checksum mismatch in zip entry #" + std::to_string(i);
					return false;
				}
			}

			if ((i + 1) < globalInfo.number_entry && unzGoToNextFile(zipFile) != UNZ_OK)
			{
				unzClose(zipFile);
				out_error = "Could not read zip entry #" + std::to_string(i + 1);
				return false;
			}
		}

		unzClose(zipFile);
		return true;
	}

	static bool DownloadFile(const std::string &url, const std::string &filename)
	{
		HINTERNET connect = InternetOpenA("Streamlabs", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);

		if (!connect)
			return false;

		HINTERNET hOpenAddress = InternetOpenUrlA(connect, url.c_str(), NULL, 0, INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_KEEP_CONNECTION, 0);

		if (!hOpenAddress)
		{
			InternetCloseHandle(connect);
			return false;
		}

		DWORD contentLength;
		DWORD contentLengthSize = sizeof(contentLength);
		if (!HttpQueryInfo(hOpenAddress, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLength, &contentLengthSize, NULL))
		{
			//blog(LOG_ERROR, "DownloadFile: HttpQueryInfo failed, last error = %d\n", GetLastError());
			InternetCloseHandle(hOpenAddress);
			InternetCloseHandle(connect);
			return false;
		}

		char dataReceived[4096];
		DWORD numberOfBytesRead = 0;

		std::ofstream outFile(filename, std::ios::binary);
		if (!outFile.is_open())
		{
			//blog(LOG_ERROR, "DownloadFile: Could not open std::ofstream outFile, last error = %d\n", GetLastError());
			InternetCloseHandle(hOpenAddress);
			InternetCloseHandle(connect);
			return false;
		}

		DWORD totalBytesRead = 0;
		while (InternetReadFile(hOpenAddress, dataReceived, 4096, &numberOfBytesRead) && numberOfBytesRead > 0)
		{
			outFile.write(dataReceived, numberOfBytesRead);
			totalBytesRead += numberOfBytesRead;
		}

		outFile.close();
		InternetCloseHandle(hOpenAddress);
		InternetCloseHandle(connect);

		if (totalBytesRead != contentLength)
		{
			//blog(LOG_ERROR, "DownloadFile: Incomplete download, last error = %d\n", GetLastError());
			std::remove(filename.c_str());
			return false;
		}

		return true;
	};

	static int HTTPRequest(const std::string &a_URI, const std::string &a_method, const std::string &a_headers, DWORD *a_pOutHttpCode, DWORD a_nTimeoutMS, std::string &response, const std::string &a_body, const std::string &a_headerAcceptType)
	{
		DWORD nValue = 128;
		InternetSetOption(NULL, INTERNET_OPTION_MAX_CONNS_PER_SERVER, (LPVOID)&nValue, sizeof(nValue));

		DWORD retVal = ERROR_SUCCESS;

		// Parse URI
		URL_COMPONENTSA uri;
		uri.dwStructSize = sizeof(uri);
		uri.dwSchemeLength = 1;
		uri.dwHostNameLength = 1;
		uri.dwUserNameLength = 1;
		uri.dwPasswordLength = 1;
		uri.dwUrlPathLength = 1;
		uri.dwExtraInfoLength = 1;
		uri.lpszScheme = NULL;
		uri.lpszHostName = NULL;
		uri.lpszUserName = NULL;
		uri.lpszPassword = NULL;
		uri.lpszUrlPath = NULL;
		uri.lpszExtraInfo = NULL;

		if (BOOL isProperURI = ::InternetCrackUrlA(a_URI.c_str(), (DWORD)a_URI.length(), 0, &uri))
		{
			std::string scheme(uri.lpszScheme, uri.dwSchemeLength);
			std::string serverName(uri.lpszHostName, uri.dwHostNameLength);
			std::string object(uri.lpszUrlPath, uri.dwUrlPathLength);
			std::string extraInfo(uri.lpszExtraInfo, uri.dwExtraInfoLength);
			std::string username(uri.lpszUserName, uri.dwUserNameLength);
			std::string password(uri.lpszPassword, uri.dwPasswordLength);

			// Open HTTP resource
			if (HINTERNET handle = ::InternetOpenA("HTTP", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0))
			{
				// Setup timeout
				if (::InternetSetOptionA(handle, INTERNET_OPTION_CONNECT_TIMEOUT, &a_nTimeoutMS, sizeof(DWORD)))
				{
					// Connect to server
					if (HINTERNET session = ::InternetConnectA(handle, serverName.c_str(), uri.nPort, username.c_str(), password.c_str(), INTERNET_SERVICE_HTTP, 0, 0))
					{
						// Request object
						DWORD dwFlags = INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_UI |
								INTERNET_FLAG_RELOAD;

						if (scheme == "https")
						{
							dwFlags |= INTERNET_FLAG_SECURE;
							// dwFlags |= INTERNET_FLAG_KEEP_CONNECTION; Not needed for independent calls
						}

						// Define the Accept Types
						std::string sHeaders = a_headers;
						LPCSTR rgpszAcceptTypes[] = {sHeaders.c_str(), NULL};

						if (HINTERNET h_istream = ::HttpOpenRequestA(session, a_method.c_str(), (object + extraInfo).c_str(), NULL, NULL, rgpszAcceptTypes, dwFlags, 0))
						{
							// Send attached data, if any
							BOOL requestOk = TRUE;

							// Append input headers
							if (a_headers.length() > 0)
								requestOk = ::HttpAddRequestHeadersA(h_istream, a_headers.c_str(), (DWORD)a_headers.length(), HTTP_ADDREQ_FLAG_ADD);

							// Ignore certificate verification
							DWORD nFlags;
							DWORD nBuffLen = sizeof(nFlags);
							::InternetQueryOptionA(h_istream, INTERNET_OPTION_SECURITY_FLAGS, (LPVOID)&nFlags, &nBuffLen);
							nFlags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
							::InternetSetOptionA(h_istream, INTERNET_OPTION_SECURITY_FLAGS, &nFlags, sizeof(nFlags));

							// Send
							if (a_body.size() > 0)
							{
								if (requestOk)
									requestOk = ::HttpSendRequestA(h_istream, NULL, 0, (void *)a_body.data(), (DWORD)a_body.size());
							}
							else if (requestOk)
							{
								requestOk = ::HttpSendRequestA(h_istream, NULL, 0, NULL, 0);
							}

							// Validate
							if (requestOk)
							{
								// Query HTTP status code
								if (a_pOutHttpCode != nullptr)
								{
									DWORD dwStatusCode = 0;
									DWORD dwSize = sizeof(dwStatusCode);

									*a_pOutHttpCode = 0;

									if (HttpQueryInfo(h_istream, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwSize, NULL))
										*a_pOutHttpCode = dwStatusCode;
								}

								// Read response
								static const DWORD SIZE = 4 * 1024;
								BYTE data[SIZE];
								DWORD size = 0;

								// Download
								do
								{
									// Read chunk of bytes
									BOOL result = ::InternetReadFile(h_istream, data, SIZE, &size);

									// Error check
									if (result)
									{
										// Write data read
										if (size > 0)
											response.append((char *)data, size);
									}
									else
									{
										retVal = GetLastError();
									}

								} while (retVal == ERROR_SUCCESS && size > 0);
							}
							else
							{
								retVal = GetLastError();
							}

							// Close handles
							::InternetCloseHandle(h_istream);
						}
						else
						{
							retVal = GetLastError();
						}

						::InternetCloseHandle(session);
					}
					else
					{
						retVal = GetLastError();
					}
				}
				else
				{
					retVal = GetLastError();
				}

				// Close internet handle
				::InternetCloseHandle(handle);
			}
			else
			{
				retVal = GetLastError();
			}
		}
		else
		{
			retVal = GetLastError();
			retVal = ERROR_INVALID_DATA;
		}

		return retVal;
	}

	static bool InstallFont(const char *fontPath)
	{
		if (AddFontResourceA(fontPath))
		{
			PostMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
			return true;
		}

		return false;
	}

	static bool KillProcess(DWORD processId)
	{
		// Then terminate the main process
		HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
		if (hProcess == NULL)
			return false;

		BOOL result = TerminateProcess(hProcess, 0);
		CloseHandle(hProcess);

		return (result != FALSE);
	}

	static bool LaunchProcess(const std::wstring &filePath)
	{
		// Extract directory from filePath
		std::wstring directory = filePath.substr(0, filePath.find_last_of(L"\\/"));

		// Initialize STARTUPINFO and PROCESS_INFORMATION
		STARTUPINFOW si = {sizeof(si)};
		PROCESS_INFORMATION pi = {};

		// Create a modifiable copy of filePath for CreateProcessW
		std::wstring modifiableFilePath = filePath;

		// Launch the process
		if (!CreateProcessW(nullptr,                                         // Application name
				    &modifiableFilePath[0],                          // Command line (modifiable)
				    nullptr,                                         // Process handle not inheritable
				    nullptr,                                         // Thread handle not inheritable
				    FALSE,                                           // Set handle inheritance to FALSE
				    0,                                               // No creation flags
				    nullptr,                                         // Use parent's environment block
				    directory.empty() ? nullptr : directory.c_str(), // Set working directory
				    &si,                                             // Pointer to STARTUPINFO structure
				    &pi                                              // Pointer to PROCESS_INFORMATION structure
				    ))
		{
			return false;
		}

		// Close the process and thread handles
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		return true;
	}
}
