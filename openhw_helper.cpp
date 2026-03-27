#include "openhw_ipc.h"

#include <Windows.h>
#include <shellapi.h>

#include <cwchar>
#include <string>

namespace {

using pfnOhmGetSystemMetrics = bool(__cdecl*)(OpenHardwareMonitorSystemMetrics*);
using pfnOhmShutdown = void(__cdecl*)();
using pfnOhmGetLastStatus = int(__cdecl*)(wchar_t*, int);

struct OpenHardwareMonitorBridgeState
{
    HMODULE module{nullptr};
    pfnOhmGetSystemMetrics getSystemMetrics{nullptr};
    pfnOhmShutdown shutdown{nullptr};
    pfnOhmGetLastStatus getLastStatus{nullptr};
};

struct PipeSecurityContext
{
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{};
    bool initialized{false};
};

void logOpenHardwareMonitorHelperEvent(const wchar_t* message)
{
    wchar_t tempPath[MAX_PATH] = {};
    const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
    if (tempLength == 0 || tempLength >= MAX_PATH)
        return;

    std::wstring logPath(tempPath, tempLength);
    logPath += L"VRCOSC.OpenHardwareMonitorHelper.log";

    HANDLE logFile = CreateFileW(logPath.c_str(),
                                 FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr,
                                 OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
    if (logFile == INVALID_HANDLE_VALUE)
        return;

    SYSTEMTIME localTime{};
    GetLocalTime(&localTime);

    wchar_t line[1024] = {};
    const int length = swprintf_s(
        line,
        L"%04u-%02u-%02uT%02u:%02u:%02u.%03u pid=%lu %ls\r\n",
        static_cast<unsigned>(localTime.wYear),
        static_cast<unsigned>(localTime.wMonth),
        static_cast<unsigned>(localTime.wDay),
        static_cast<unsigned>(localTime.wHour),
        static_cast<unsigned>(localTime.wMinute),
        static_cast<unsigned>(localTime.wSecond),
        static_cast<unsigned>(localTime.wMilliseconds),
        static_cast<unsigned long>(GetCurrentProcessId()),
        message ? message : L"");

    if (length > 0) {
        DWORD written = 0;
        WriteFile(logFile, line, static_cast<DWORD>(length * sizeof(wchar_t)), &written, nullptr);
    }

    CloseHandle(logFile);
}

void logOpenHardwareMonitorHelperMessage(const std::wstring& message)
{
    logOpenHardwareMonitorHelperEvent(message.c_str());
}

SECURITY_ATTRIBUTES* openHardwareMonitorPipeSecurityAttributes()
{
    static PipeSecurityContext context{};
    if (!context.initialized) {
        context.initialized = InitializeSecurityDescriptor(&context.descriptor, SECURITY_DESCRIPTOR_REVISION) != FALSE
            && SetSecurityDescriptorDacl(&context.descriptor, TRUE, nullptr, FALSE) != FALSE;

        if (context.initialized) {
            context.attributes.nLength = sizeof(context.attributes);
            context.attributes.lpSecurityDescriptor = &context.descriptor;
            context.attributes.bInheritHandle = FALSE;
        }
    }

    return context.initialized ? &context.attributes : nullptr;
}

bool readExact(HANDLE handle, void* buffer, DWORD size)
{
    auto* bytes = static_cast<unsigned char*>(buffer);
    DWORD totalRead = 0;
    while (totalRead < size) {
        DWORD chunkRead = 0;
        if (!ReadFile(handle, bytes + totalRead, size - totalRead, &chunkRead, nullptr)
            || chunkRead == 0) {
            return false;
        }
        totalRead += chunkRead;
    }
    return true;
}

bool writeExact(HANDLE handle, const void* buffer, DWORD size)
{
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    DWORD totalWritten = 0;
    while (totalWritten < size) {
        DWORD chunkWritten = 0;
        if (!WriteFile(handle, bytes + totalWritten, size - totalWritten, &chunkWritten, nullptr)
            || chunkWritten == 0) {
            return false;
        }
        totalWritten += chunkWritten;
    }
    return true;
}

HANDLE openParentProcessFromCommandLine()
{
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments)
        return nullptr;

    HANDLE parentProcess = nullptr;
    if (argumentCount >= 2) {
        const DWORD parentPid = static_cast<DWORD>(wcstoul(arguments[1], nullptr, 10));
        if (parentPid != 0) {
            parentProcess = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
            logOpenHardwareMonitorHelperMessage(
                L"parsed parent pid=" + std::to_wstring(parentPid)
                + std::wstring(L" open=")
                + (parentProcess ? L"true" : L"false"));
        }
    }

    LocalFree(arguments);
    return parentProcess;
}

std::wstring currentDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};

    std::wstring path(modulePath, length);
    const std::size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

bool loadOpenHardwareMonitorBridge(OpenHardwareMonitorBridgeState& state)
{
    if (state.getSystemMetrics)
        return true;

    const std::wstring directory = currentDirectory();
    if (directory.empty())
        return false;

    const std::wstring bridgePath = directory + L"\\OpenHardwareMonitorBridge.dll";
    logOpenHardwareMonitorHelperMessage(L"loading bridge path=" + bridgePath);

    state.module = LoadLibraryW(bridgePath.c_str());
    if (!state.module)
        return false;

    state.getSystemMetrics = reinterpret_cast<pfnOhmGetSystemMetrics>(
        GetProcAddress(state.module, "OHM_GetSystemMetrics"));
    state.shutdown = reinterpret_cast<pfnOhmShutdown>(
        GetProcAddress(state.module, "OHM_Shutdown"));
    state.getLastStatus = reinterpret_cast<pfnOhmGetLastStatus>(
        GetProcAddress(state.module, "OHM_GetLastStatus"));

    if (state.getSystemMetrics)
        return true;

    if (state.module) {
        FreeLibrary(state.module);
        state.module = nullptr;
    }

    state.shutdown = nullptr;
    state.getLastStatus = nullptr;
    return false;
}

void unloadOpenHardwareMonitorBridge(OpenHardwareMonitorBridgeState& state)
{
    if (state.shutdown)
        state.shutdown();

    state.shutdown = nullptr;
    state.getSystemMetrics = nullptr;
    state.getLastStatus = nullptr;

    if (state.module) {
        FreeLibrary(state.module);
        state.module = nullptr;
    }
}

void collectSystemMetrics(OpenHardwareMonitorBridgeState& bridge,
                          OpenHardwareMonitorHelperResponse& response)
{
    if (!loadOpenHardwareMonitorBridge(bridge)) {
        response.success = 0;
        response.detailCode = OpenHardwareMonitorHelperDetailCode_BridgeLoadFailed;
        return;
    }

    const bool hasMetrics = bridge.getSystemMetrics(&response.systemMetrics);
    response.success = hasMetrics ? 1u : 0u;
    response.detailCode = hasMetrics
        ? OpenHardwareMonitorHelperDetailCode_None
        : OpenHardwareMonitorHelperDetailCode_NoMetricsAvailable;

    if (bridge.getLastStatus) {
        bridge.getLastStatus(response.detailMessage,
                             static_cast<int>(kOpenHardwareMonitorDetailMessageCapacity));
    }

    logOpenHardwareMonitorHelperMessage(
        L"get system metrics success=" + std::to_wstring(hasMetrics ? 1u : 0u));
}

bool connectClientOrParentExit(HANDLE pipe, HANDLE parentProcess, bool& parentExited)
{
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent)
        return false;

    bool connected = false;
    const BOOL connectResult = ConnectNamedPipe(pipe, &overlapped);
    if (connectResult) {
        connected = true;
    } else {
        switch (GetLastError()) {
        case ERROR_IO_PENDING: {
            HANDLE waitHandles[2] = {overlapped.hEvent, parentProcess};
            const DWORD waitCount = parentProcess ? 2u : 1u;
            const DWORD waitResult = WaitForMultipleObjects(waitCount, waitHandles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD transferred = 0;
                connected = GetOverlappedResult(pipe, &overlapped, &transferred, FALSE) != FALSE;
                logOpenHardwareMonitorHelperEvent(connected ? L"pipe client connected" : L"pipe connect failed after pending");
            } else if (waitResult == WAIT_OBJECT_0 + 1) {
                parentExited = true;
                logOpenHardwareMonitorHelperEvent(L"parent exited while waiting for pipe client");
                CancelIoEx(pipe, &overlapped);
            }
            break;
        }
        case ERROR_PIPE_CONNECTED:
            connected = true;
            logOpenHardwareMonitorHelperEvent(L"pipe already connected");
            break;
        default:
            logOpenHardwareMonitorHelperMessage(
                L"ConnectNamedPipe failed error=" + std::to_wstring(GetLastError()));
            break;
        }
    }

    CloseHandle(overlapped.hEvent);
    return connected;
}

bool handleClient(HANDLE pipe,
                  OpenHardwareMonitorBridgeState& bridge,
                  bool& shouldStop)
{
    OpenHardwareMonitorHelperRequest request{};
    if (!readExact(pipe, &request, sizeof(request)))
        return false;

    logOpenHardwareMonitorHelperMessage(
        L"received command=" + std::to_wstring(request.command));

    OpenHardwareMonitorHelperResponse response{};
    response.version = kOpenHardwareMonitorProtocolVersion;
    response.detailCode = OpenHardwareMonitorHelperDetailCode_None;

    if (request.version != kOpenHardwareMonitorProtocolVersion) {
        response.success = 0;
        response.detailCode = OpenHardwareMonitorHelperDetailCode_ProtocolMismatch;
        return writeExact(pipe, &response, sizeof(response));
    }

    switch (request.command) {
    case OpenHardwareMonitorHelperCommand_GetSystemMetrics:
        collectSystemMetrics(bridge, response);
        break;
    case OpenHardwareMonitorHelperCommand_Shutdown:
        response.success = 1;
        response.detailCode = OpenHardwareMonitorHelperDetailCode_None;
        shouldStop = true;
        logOpenHardwareMonitorHelperEvent(L"shutdown command received");
        break;
    default:
        response.success = 0;
        response.detailCode = OpenHardwareMonitorHelperDetailCode_UnsupportedCommand;
        break;
    }

    return writeExact(pipe, &response, sizeof(response));
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    logOpenHardwareMonitorHelperEvent(L"helper process started");

    HANDLE parentProcess = openParentProcessFromCommandLine();
    if (!parentProcess)
        return 1;

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\VRCOSC.OpenHardwareMonitorHelper");
    if (!mutex) {
        CloseHandle(parentProcess);
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        logOpenHardwareMonitorHelperEvent(L"helper mutex already exists, exiting");
        CloseHandle(mutex);
        CloseHandle(parentProcess);
        return 0;
    }

    OpenHardwareMonitorBridgeState bridge;
    bool shouldStop = false;

    while (!shouldStop) {
        HANDLE pipe = CreateNamedPipeW(
            kOpenHardwareMonitorPipeName,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1,
            sizeof(OpenHardwareMonitorHelperResponse),
            sizeof(OpenHardwareMonitorHelperRequest),
            0,
            openHardwareMonitorPipeSecurityAttributes());

        if (pipe == INVALID_HANDLE_VALUE)
            break;

        bool parentExited = false;
        const bool connected = connectClientOrParentExit(pipe, parentProcess, parentExited);
        if (parentExited)
            shouldStop = true;

        if (connected) {
            handleClient(pipe, bridge, shouldStop);
            FlushFileBuffers(pipe);
            DisconnectNamedPipe(pipe);
        }

        CloseHandle(pipe);
    }

    unloadOpenHardwareMonitorBridge(bridge);
    logOpenHardwareMonitorHelperEvent(L"helper process exiting");
    CloseHandle(mutex);
    CloseHandle(parentProcess);
    return 0;
}
