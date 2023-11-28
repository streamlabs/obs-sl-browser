#pragma once

#include <string>
#include <windows.h>
#include <wininet.h>
#include <fstream>
#include <TlHelp32.h>

#include "deps/minizip/unzip.h"

#pragma comment(lib, "wininet.lib")

namespace WindowsFunctions
{
	static void ForceForegroundWindow(HWND focusOnWindowHandle)
	{
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

	static bool Unzip(const std::string &filepath, std::vector<std::string> &output)
	{
		unzFile zipFile = unzOpen(filepath.c_str());
		if (!zipFile)
		{
			// Failed to open the zip file
			return false;
		}

		unz_global_info globalInfo;
		if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
		{
			unzClose(zipFile);
			return false;
		}

		char buffer[4096]; // Adjust buffer size as needed

		for (uLong i = 0; i < globalInfo.number_entry; ++i)
		{
			char filename[256];
			unz_file_info fileInfo;

			if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, sizeof(filename), NULL, 0, NULL, 0) != UNZ_OK)
			{
				unzClose(zipFile);
				return false;
			}

			std::filesystem::path fullOutputPath = std::filesystem::path(filepath).parent_path() / filename;

			if (filename[strlen(filename) - 1] == '/')
			{
				// It's a directory
				std::filesystem::create_directories(fullOutputPath);
			}
			else
			{
				// It's a file
				if (unzOpenCurrentFile(zipFile) != UNZ_OK)
				{
					unzClose(zipFile);
					return false;
				}

				std::filesystem::create_directories(fullOutputPath.parent_path());
				std::ofstream outFile(fullOutputPath, std::ios::binary);
				int error = UNZ_OK;
				do
				{
					error = unzReadCurrentFile(zipFile, buffer, sizeof(buffer));
					if (error < 0)
					{
						unzCloseCurrentFile(zipFile);
						unzClose(zipFile);
						return false;
					}
					if (error > 0)
					{
						outFile.write(buffer, error);
					}
				} while (error > 0);

				outFile.close();
				auto fpstr = fullOutputPath.string();

				// Use '\' for all just because
				for (size_t i = 0; i < fpstr.size(); ++i)
				{
					if (fpstr[i] == '/')
						fpstr[i] = '\\';
				}

				output.push_back(fpstr);

				if (unzCloseCurrentFile(zipFile) != UNZ_OK)
				{
					unzClose(zipFile);
					return false;
				}
			}

			if ((i + 1) < globalInfo.number_entry)
			{
				if (unzGoToNextFile(zipFile) != UNZ_OK)
				{
					unzClose(zipFile);
					return false;
				}
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
			SendMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
			return true;
		}

		//blog(LOG_ERROR, "InstallFont: Failed, last error = %d\n", GetLastError());
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
