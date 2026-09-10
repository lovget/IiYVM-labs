#include "PowerMonitor.h"

#include <windows.h>
#include <powrprof.h>

#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "PowrProf.lib")

BatteryInfo PowerMonitor::GetBatteryInfo() const
{
    BatteryInfo info;

    SYSTEM_POWER_STATUS status{};

    if (!GetSystemPowerStatus(&status))
    {
        return info;
    }

    info.percent = status.BatteryLifePercent;
    info.connected = status.ACLineStatus == 1;

    info.charging =
        (status.BatteryFlag &
            BATTERY_FLAG_CHARGING) != 0;

    info.remainingSeconds =
        status.BatteryLifeTime;

    return info;
}


PowerCapabilities PowerMonitor::GetPowerCapabilities() const
{
    PowerCapabilities result;

    SYSTEM_POWER_CAPABILITIES capabilities{};

    if (!GetPwrCapabilities(&capabilities))
    {
        return result;
    }

    result.sleepSupported =
        capabilities.AoAc ||
        capabilities.SystemS1 ||
        capabilities.SystemS2 ||
        capabilities.SystemS3;

    result.hibernateSupported =
        capabilities.SystemS4;

    result.wakeSupported =
        capabilities.FullWake;

    return result;
}


static std::wstring GetSchemeName(
    const GUID& guid)
{
    DWORD size = 0;

    DWORD result = PowerReadFriendlyName(
        nullptr,
        &guid,
        nullptr,
        nullptr,
        nullptr,
        &size
    );

    if (result != ERROR_SUCCESS &&
        result != ERROR_MORE_DATA)
    {
        return L"Неизвестная схема";
    }

    if (size == 0)
    {
        return L"Без названия";
    }

    std::vector<UCHAR> buffer(size);

    result = PowerReadFriendlyName(
        nullptr,
        &guid,
        nullptr,
        nullptr,
        buffer.data(),
        &size
    );

    if (result != ERROR_SUCCESS)
    {
        return L"Неизвестная схема";
    }

    return std::wstring(
        reinterpret_cast<wchar_t*>(
            buffer.data()
            )
    );
}


std::vector<PowerSchemeInfo>
PowerMonitor::GetPowerSchemes() const
{
    std::vector<PowerSchemeInfo> schemes;

    for (ULONG index = 0; ; ++index)
    {
        GUID guid{};

        DWORD size = sizeof(GUID);

        DWORD result = PowerEnumerate(
            nullptr,
            nullptr,
            nullptr,
            ACCESS_SCHEME,
            index,
            reinterpret_cast<UCHAR*>(
                &guid
                ),
            &size
        );

        if (result != ERROR_SUCCESS)
        {
            break;
        }

        PowerSchemeInfo scheme;

        scheme.guid = guid;
        scheme.name = GetSchemeName(guid);

        schemes.push_back(scheme);
    }

    return schemes;
}


std::wstring
PowerMonitor::GetActivePowerSchemeName() const
{
    GUID* activeGuid = nullptr;

    DWORD result = PowerGetActiveScheme(
        nullptr,
        &activeGuid
    );

    if (result != ERROR_SUCCESS ||
        activeGuid == nullptr)
    {
        return L"Неизвестно";
    }

    std::wstring name =
        GetSchemeName(*activeGuid);

    LocalFree(activeGuid);

    return name;
}


bool PowerMonitor::SetActivePowerScheme(
    const GUID& guid) const
{
    DWORD result = PowerSetActiveScheme(
        nullptr,
        &guid
    );

    return result == ERROR_SUCCESS;
}


bool PowerMonitor::EnableShutdownPrivilege() const
{
    HANDLE token = nullptr;

    if (!OpenProcessToken(
        GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES |
        TOKEN_QUERY,
        &token))
    {
        return false;
    }

    TOKEN_PRIVILEGES tp{};

    tp.PrivilegeCount = 1;

    if (!LookupPrivilegeValueW(
        nullptr,
        SE_SHUTDOWN_NAME,
        &tp.Privileges[0].Luid))
    {
        CloseHandle(token);
        return false;
    }

    tp.Privileges[0].Attributes =
        SE_PRIVILEGE_ENABLED;

    SetLastError(ERROR_SUCCESS);

    if (!AdjustTokenPrivileges(
        token,
        FALSE,
        &tp,
        sizeof(tp),
        nullptr,
        nullptr))
    {
        CloseHandle(token);
        return false;
    }

    DWORD error = GetLastError();

    CloseHandle(token);

    if (error != ERROR_SUCCESS)
    {
        SetLastError(error);
        return false;
    }

    return true;
}


bool PowerMonitor::Sleep() const
{
    if (!GetPowerCapabilities().sleepSupported)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    if (!EnableShutdownPrivilege())
    {
        return false;
    }

    SetLastError(ERROR_SUCCESS);

    return SetSuspendState(
        FALSE,
        FALSE,
        FALSE
    ) == TRUE;
}


bool PowerMonitor::Hibernate() const
{
    if (!GetPowerCapabilities().hibernateSupported)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        return false;
    }

    if (!EnableShutdownPrivilege())
    {
        return false;
    }

    SetLastError(ERROR_SUCCESS);

    return SetSuspendState(
        TRUE,
        FALSE,
        FALSE
    ) == TRUE;
}


std::wstring
PowerMonitor::GetLastErrorText() const
{
    DWORD error = GetLastError();

    if (error == ERROR_SUCCESS)
    {
        return L"Ошибок нет";
    }

    LPWSTR buffer = nullptr;

    DWORD size = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(
            &buffer
            ),
        0,
        nullptr
    );

    std::wstring result;

    if (size > 0 &&
        buffer != nullptr)
    {
        result.assign(
            buffer,
            size
        );

        LocalFree(buffer);
    }
    else
    {
        result = L"Неизвестная ошибка";
    }

    return result;
}


std::wstring
PowerMonitor::FormatTime(
    DWORD seconds)
{
    if (seconds == (DWORD)-1)
    {
        return L"Недоступно";
    }

    DWORD hours = seconds / 3600;
    DWORD minutes = (seconds % 3600) / 60;
    DWORD secs = seconds % 60;

    std::wstringstream stream;

    stream
        << std::setfill(L'0')
        << std::setw(2)
        << hours
        << L":"
        << std::setw(2)
        << minutes
        << L":"
        << std::setw(2)
        << secs;

    return stream.str();
}