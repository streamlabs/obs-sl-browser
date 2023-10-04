#pragma once

#include <string>
#include <windows.h>
#include <wininet.h>
#include <fstream>

#include "deps/minizip/unzip.h"

#pragma comment(lib, "wininet.lib")

namespace Util
{
	static bool Unzip(const std::string &filepath, std::vector<std::string> &output)
	{
		unzFile zipFile = unzOpen(filepath.c_str());
		if (!zipFile)
		{
			blog(LOG_ERROR, "Unzip: Unable to open zip file: %s", filepath.c_str());
			return false;
		}

		std::filesystem::path outputDir = std::filesystem::path(filepath).parent_path();
		char buffer[4096];

		unz_global_info globalInfo;
		if (unzGetGlobalInfo(zipFile, &globalInfo) != UNZ_OK)
		{
			blog(LOG_ERROR, "Unzip: Could not read file global info: %s", filepath.c_str());
			unzClose(zipFile);
			return false;
		}

		for (uLong i = 0; i < globalInfo.number_entry; ++i)
		{
			unz_file_info fileInfo;
			char filename[256];
			if (unzGetCurrentFileInfo(zipFile, &fileInfo, filename, 256, NULL, 0, NULL, 0) != UNZ_OK)
			{
				blog(LOG_ERROR, "Unzip: Could not read file info: %s", filepath.c_str());
				unzClose(zipFile);
				return false;
			}

			const std::string fullOutputPath = outputDir.string() + '/' + filename;

			// If the file in the zip archive is a directory, continue to next file
			if (filename[strlen(filename) - 1] == '/')
			{
				if ((i + 1) < globalInfo.number_entry)
				{
					if (unzGoToNextFile(zipFile) != UNZ_OK)
					{
						blog(LOG_ERROR, "Unzip: Could not read next file: %s", filepath.c_str());
						unzClose(zipFile);
						return false;
					}
				}

				continue;
			}

			if (unzOpenCurrentFile(zipFile) != UNZ_OK)
			{
				blog(LOG_ERROR, "Unzip: Could not open current file: %s", filepath.c_str());
				unzClose(zipFile);
				return false;
			}

			// Create necessary directories
			std::filesystem::path pathToFile(fullOutputPath);
			if (fileInfo.uncompressed_size > 0)
				std::filesystem::create_directories(pathToFile.parent_path());
			
			std::ofstream outFile(fullOutputPath, std::ios::binary);
			int error = UNZ_OK;
			do
			{
				error = unzReadCurrentFile(zipFile, buffer, sizeof(buffer));
				if (error < 0)
				{
					blog(LOG_ERROR, "Unzip: Error %d with zipfile in unzReadCurrentFile: %s", error, filepath.c_str());
					break;
				}

				if (error > 0)
					outFile.write(buffer, error);
			}
			while (error > 0);

			outFile.close();

			// Adding the file path to the output vector
			output.push_back(fullOutputPath);

			if (unzCloseCurrentFile(zipFile) != UNZ_OK)
				blog(LOG_ERROR, "Unzip: Could not close file: %s", filepath.c_str());

			if ((i + 1) < globalInfo.number_entry)
			{
				if (unzGoToNextFile(zipFile) != UNZ_OK)
				{
					blog(LOG_ERROR, "Unzip: Could not read next file: %s", filepath.c_str());
					unzClose(zipFile);
					return false;
				}
			}
		}

		unzClose(zipFile);
		return true;
	};

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
			blog(LOG_ERROR, "DownloadFile: HttpQueryInfo failed, last error = %d\n", GetLastError());
			InternetCloseHandle(hOpenAddress);
			InternetCloseHandle(connect);
			return false;
		}

		char dataReceived[4096];
		DWORD numberOfBytesRead = 0;

		std::ofstream outFile(filename, std::ios::binary);
		if (!outFile.is_open())
		{
			blog(LOG_ERROR, "DownloadFile: Could not open std::ofstream outFile, last error = %d\n", GetLastError());
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
			blog(LOG_ERROR, "DownloadFile: Incomplete download, last error = %d\n", GetLastError());
			std::remove(filename.c_str());
			return false;
		}

		return true;
	};

	static bool InstallFont(const char *fontPath)
	{
		if (AddFontResourceA(fontPath))
		{
			SendMessage(HWND_BROADCAST, WM_FONTCHANGE, 0, 0);
			return true;
		}

		blog(LOG_ERROR, "InstallFont: Failed, last error = %d\n", GetLastError());
		return false;
	}

}
