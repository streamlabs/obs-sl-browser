#include "WebServer.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <obs.h>

#pragma comment(lib, "Ws2_32.lib")

WebServer::WebServer() {

}

WebServer::~WebServer()
{
	stop();
}

bool WebServer::start(const int32_t port)
{
	if (m_launching || m_running)
	{
		m_err = "Already running";
		return false;
	}

	m_launching = true;
	m_workerThread = std::thread(&WebServer::workerThread, this);

	while (m_launching)
		::Sleep(1);

	return m_running;
}

void WebServer::stop()
{
	m_running = false;

	if (m_workerThread.joinable())
		m_workerThread.join();
}

std::string WebServer::getToken()
{
	std::lock_guard<std::mutex> grd(m_tokenMtx);
	return m_token;
}

void WebServer::workerThread()
{
	WSADATA wsaData;
	int iResult;

	SOCKET listenSocket = INVALID_SOCKET;
	SOCKET clientSocket = INVALID_SOCKET;

	struct addrinfo *result = NULL;
	struct addrinfo hints;

	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		m_err = "WSAStartup failed with error: " + std::to_string(iResult);
		m_launching = false;
		return;
	}

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	// Convert m_port to string or use "0" if m_port is 0.
	std::string portStr = m_port == 0 ? "0" : std::to_string(m_port);

	// Resolve the local address and port to be used by the server
	iResult = getaddrinfo(NULL, portStr.c_str(), &hints, &result);
	if (iResult != 0)
	{
		m_err = "getaddrinfo failed with error: " + std::to_string(iResult);
		WSACleanup();
		m_launching = false;
		return;
	}

	listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (listenSocket == INVALID_SOCKET)
	{
		m_err = "socket failed with error: " + std::to_string(WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		m_launching = false;
		return;
	}

	// Setup the TCP listening socket
	iResult = bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		m_err = "bind failed with error: " + std::to_string(WSAGetLastError());
		freeaddrinfo(result);
		closesocket(listenSocket);
		WSACleanup();
		m_launching = false;
		return;
	}

	// Retrieve and set the port assigned by OS to m_port, if m_port was 0.
	if (m_port == 0)
	{
		struct sockaddr_in sin;
		socklen_t len = sizeof(sin);
		if (getsockname(listenSocket, (struct sockaddr *)&sin, &len) != -1)
			m_port = ntohs(sin.sin_port);
	}

	freeaddrinfo(result);

	iResult = listen(listenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR)
	{
		m_err = "listen failed with error: " + std::to_string(WSAGetLastError());
		closesocket(listenSocket);
		WSACleanup();
		m_launching = false;
		return;
	}

	printf("WebServer Listening on port %d\n", m_port);

	m_running = true;
	m_launching = false;

	while (m_running)
	{
		timeval timeout;
		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(listenSocket, &readSet);

		// 1 second listen timeout
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;
		int selectResult = select(0, &readSet, NULL, NULL, &timeout);

		if (selectResult > 0 && FD_ISSET(listenSocket, &readSet))
		{
			// Accept a client socket
			clientSocket = accept(listenSocket, NULL, NULL);
			if (clientSocket == INVALID_SOCKET)
			{
				blog(LOG_ERROR, "accept failed with error: %d\n", WSAGetLastError());
				closesocket(listenSocket);
				WSACleanup();
				m_running = false;
				return;
			}
		}
		else if (selectResult == 0)
		{
			// Timeout, no incoming connections, continue to the next iteration
			continue;
		}
		else
		{
			blog(LOG_ERROR, "select failed with error: %d\n", WSAGetLastError());
			closesocket(listenSocket);
			WSACleanup();
			m_running = false;
			return;
		}

		FD_ZERO(&readSet);
		FD_SET(clientSocket, &readSet);

		// 2 seconds timeout
		timeout.tv_sec = 2;
		timeout.tv_usec = 0;
		selectResult = select(0, &readSet, NULL, NULL, &timeout);

		if (selectResult > 0 && FD_ISSET(clientSocket, &readSet))
		{
			char recvbuf[8192];
			int recvbuflen = 8190;
			int iResult = recv(clientSocket, recvbuf, recvbuflen, 0);
			if (iResult > 0)
			{
				recvbuf[iResult] = 0;
				printf("WebServer received...\n\n%s\n\n", recvbuf);
				blog(LOG_INFO, "Received: %s\n", recvbuf);

				auto extractToken = [&](const std::string &request) {
					std::string key = "GET " + m_expectedReferer;
					size_t pos = request.find(key);
					if (pos == std::string::npos)
					{
						blog(LOG_INFO, "Did not find key %s\n", key.c_str());
						return std::string();
					}

					blog(LOG_INFO, "Found key, looking for HTTP followup\n");

					size_t start = pos + key.length();
					size_t end = request.find(" HTTP", start);

					if (end == std::string::npos)
					{
						printf("Did not find followup\n");
						return std::string();
					}

					blog(LOG_INFO, "Extracting token");
					return request.substr(start, end - start);
				};

				std::string token = extractToken(recvbuf);

				if (token.size() > 0)
				{
					blog(LOG_INFO, "extractToken success");
					std::lock_guard<std::mutex> grd(m_tokenMtx);
					m_token = token;
				}
				else
				{

					blog(LOG_INFO, "extractToken failed");
				}
			}
			else
			{
				blog(LOG_ERROR, "recv failed: %d\n", WSAGetLastError());
			}
		}
		else if (selectResult == 0)
		{
			blog(LOG_ERROR, "Timeout: client did not send any data.\n");
		}
		else
		{
			blog(LOG_ERROR, "select failed with error: %d\n", WSAGetLastError());
		}

		// 2 seconds timeout
		struct timeval tv;
		tv.tv_sec = 2; 
		tv.tv_usec = 0;

		if (setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv)) < 0)
			blog(LOG_ERROR, "Failed to set send timeout: %d\n", WSAGetLastError());
		
		std::string response = "HTTP/1.1 302 Found\r\n";
		response += "Location: " + m_redirectUrl + "\r\n\r\n";

		int iSendResult = send(clientSocket, response.c_str(), (int)response.length(), 0);
		if (iSendResult == SOCKET_ERROR)
			blog(LOG_ERROR, "send failed with error: %d\n", WSAGetLastError());

		closesocket(clientSocket);
	}

	// No longer need server socket
	closesocket(listenSocket);
	WSACleanup();
}
