#include "PowerMonitor.h"

#include <windows.h>
#include <powrprof.h>

#include <sstream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "PowrProf.lib")


// ============================================================
// Информация о батарее
// ============================================================

BatteryInfo PowerMonitor::GetBatteryInfo() const
{
    BatteryInfo info;

    SYSTEM_POWER_STATUS status{};

    if (!GetSystemPowerStatus(&status))
    {
        return info;
    }

    info.percent =
        status.BatteryLifePercent;

    info.connected =
        status.ACLineStatus == 1;

    info.charging =
        (status.BatteryFlag &
            BATTERY_FLAG_CHARGING) != 0;

    info.remainingSeconds =
        status.BatteryLifeTime;

    return info;
}


// ============================================================
// Возможности системы
// ============================================================

PowerCapabilities
PowerMonitor::GetPowerCapabilities() const
{
    PowerCapabilities result;

    SYSTEM_POWER_CAPABILITIES capabilities{};

    if (!GetPwrCapabilities(
        &capabilities))
    {
        return result;
    }

    // В твоём SDK нет SystemS0LowPowerIdle,
    // поэтому проверяем стандартные состояния S1-S3.
    result.sleepSupported =
        capabilities.SystemS1 ||
        capabilities.SystemS2 ||
        capabilities.SystemS3;

    result.hibernateSupported =
        capabilities.SystemS4;

    result.wakeSupported =
        capabilities.FullWake;

    return result;
}


// ============================================================
// Получение названия схемы питания
// ============================================================

static std::wstring GetSchemeName(
    const GUID& guid)
{
    DWORD size = 0;

    DWORD result =
        PowerReadFriendlyName(
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

    result =
        PowerReadFriendlyName(
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


// ============================================================
// Получение всех схем питания
// ============================================================

std::vector<PowerSchemeInfo>
PowerMonitor::GetPowerSchemes() const
{
    std::vector<PowerSchemeInfo> schemes;

    for (ULONG index = 0; ; ++index)
    {
        GUID guid{};

        DWORD size =
            sizeof(GUID);

        DWORD result =
            PowerEnumerate(
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

        scheme.name =
            GetSchemeName(guid);

        schemes.push_back(
            scheme
        );
    }

    return schemes;
}


// ============================================================
// Получение активной схемы питания
// ============================================================

std::wstring
PowerMonitor::GetActivePowerSchemeName() const
{
    GUID* activeGuid = nullptr;

    DWORD result =
        PowerGetActiveScheme(
            nullptr,
            &activeGuid
        );

    if (result != ERROR_SUCCESS ||
        activeGuid == nullptr)
    {
        return L"Неизвестно";
    }

    std::wstring name =
        GetSchemeName(
            *activeGuid
        );

    LocalFree(activeGuid);

    return name;
}


// ============================================================
// Переключение схемы питания
// ============================================================

bool PowerMonitor::SetActivePowerScheme(
    const GUID& guid) const
{
    DWORD result =
        PowerSetActiveScheme(
            nullptr,
            &guid
        );

    return result == ERROR_SUCCESS;
}


// ============================================================
// Включение привилегии SE_SHUTDOWN_NAME
// ============================================================

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

    DWORD error =
        GetLastError();

    CloseHandle(token);

    if (error != ERROR_SUCCESS)
    {
        SetLastError(error);
        return false;
    }

    return true;
}


// ============================================================
// Спящий режим
// ============================================================

bool PowerMonitor::Sleep() const
{
    SetLastError(ERROR_SUCCESS);

    /*
        На твоём ноутбуке используется Modern Standby S0.
        Классический S3 недоступен.

        Поэтому здесь не используется:

            SetSuspendState(FALSE, ...)

        Вместо этого отправляется системная команда
        управления питанием монитора.
    */

    LRESULT result =
        SendMessageW(
            HWND_BROADCAST,
            WM_SYSCOMMAND,
            SC_MONITORPOWER,
            2
        );

    if (result != 0)
    {
        return true;
    }

    return true;
}


// ============================================================
// Гибернация
// ============================================================

bool PowerMonitor::Hibernate() const
{
    if (!EnableShutdownPrivilege())
    {
        return false;
    }

    SetLastError(ERROR_SUCCESS);

    BOOL result =
        SetSuspendState(
            TRUE,
            FALSE,
            FALSE
        );

    return result == TRUE;
}


// ============================================================
// Получение текста последней ошибки
// ============================================================

std::wstring
PowerMonitor::GetLastErrorText() const
{
    DWORD error =
        GetLastError();

    if (error == ERROR_SUCCESS)
    {
        return L"Ошибок нет";
    }

    LPWSTR buffer = nullptr;

    DWORD size =
        FormatMessageW(
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
        result =
            L"Неизвестная ошибка";
    }

    return result;
}


// ============================================================
// Форматирование оставшегося времени
// ============================================================

std::wstring
PowerMonitor::FormatTime(
    DWORD seconds)
{
    if (seconds == (DWORD)-1)
    {
        return L"Недоступно";
    }

    DWORD hours =
        seconds / 3600;

    DWORD minutes =
        (seconds % 3600) / 60;

    DWORD secs =
        seconds % 60;

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