#pragma once

#include <windows.h>
#include <string>
#include <vector>

struct BatteryInfo
{
    BYTE percent = 255;
    bool connected = false;
    bool charging = false;
    DWORD remainingSeconds = (DWORD)-1;
};

struct PowerCapabilities
{
    bool sleepSupported = false;
    bool hibernateSupported = false;
    bool wakeSupported = false;
};

struct PowerSchemeInfo
{
    GUID guid{};
    std::wstring name;
};

class PowerMonitor
{
public:
    BatteryInfo GetBatteryInfo() const;

    PowerCapabilities GetPowerCapabilities() const;

    std::vector<PowerSchemeInfo> GetPowerSchemes() const;

    std::wstring GetActivePowerSchemeName() const;

    bool SetActivePowerScheme(const GUID& guid) const;

    bool Sleep() const;

    bool Hibernate() const;

    std::wstring GetLastErrorText() const;

    static std::wstring FormatTime(DWORD seconds);

private:
    bool EnableShutdownPrivilege() const;
    bool RequestSuspendState(bool hibernate) const;
};