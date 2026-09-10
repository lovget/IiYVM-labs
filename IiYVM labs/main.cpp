#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>

#include <string>
#include <vector>
#include <fstream>

#include "PowerMonitor.h"

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

PowerMonitor g_power;

ULONG_PTR g_gdiplusToken = 0;

bool g_showJournal = false;

enum JournalFilter
{
    FILTER_ALL = 0,
    FILTER_CHARGER,
    FILTER_CHARGE,
    FILTER_SLEEP
};

JournalFilter g_filter = FILTER_ALL;

std::vector<std::wstring> g_journal;

BatteryInfo g_lastBattery;

bool g_firstBatteryCheck = true;


std::wstring GetJournalPath()
{
    wchar_t path[MAX_PATH]{};

    DWORD length = GetModuleFileNameW(
        nullptr,
        path,
        MAX_PATH
    );

    if (length == 0 ||
        length >= MAX_PATH)
    {
        return L"power_log.txt";
    }

    std::wstring directory(
        path,
        length
    );

    size_t separator = directory.find_last_of(
        L"\\/"
    );

    if (separator == std::wstring::npos)
    {
        return L"power_log.txt";
    }

    return directory.substr(
        0,
        separator + 1
    ) + L"power_log.txt";
}


std::string WideToUtf8(
    const std::wstring& text)
{
    if (text.empty())
        return std::string();

    int requiredSize = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (requiredSize <= 0)
        return std::string();

    std::string result(
        static_cast<size_t>(requiredSize),
        '\0'
    );

    int converted = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.c_str(),
        static_cast<int>(text.size()),
        &result[0],
        requiredSize,
        nullptr,
        nullptr
    );

    if (converted <= 0)
        return std::string();

    result.resize(
        static_cast<size_t>(converted)
    );

    return result;
}


std::wstring Utf8ToWide(
    const std::string& text)
{
    if (text.empty())
        return std::wstring();

    int requiredSize = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );

    if (requiredSize <= 0)
        return std::wstring();

    std::wstring result(
        static_cast<size_t>(requiredSize),
        L'\0'
    );

    if (MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        &result[0],
        requiredSize) <= 0)
    {
        return std::wstring();
    }

    return result;
}


std::wstring CurrentTime()
{
    SYSTEMTIME st{};

    GetLocalTime(&st);

    wchar_t buffer[64]{};

    swprintf_s(
        buffer,
        L"%02d:%02d:%02d",
        st.wHour,
        st.wMinute,
        st.wSecond
    );

    return std::wstring(buffer);
}


void AddJournal(
    const std::wstring& text)
{
    std::wstring line =
        L"[" +
        CurrentTime() +
        L"] " +
        text;

    g_journal.push_back(line);

    if (g_journal.size() > 500)
    {
        g_journal.erase(
            g_journal.begin()
        );
    }

    std::ofstream file(
        GetJournalPath(),
        std::ios::app |
        std::ios::binary
    );

    if (file)
    {
        file << WideToUtf8(line) << "\n";
    }
}


void LoadJournal()
{
    std::ifstream file(
        GetJournalPath(),
        std::ios::binary
    );

    std::string line;

    while (std::getline(file, line))
    {
        if (!line.empty() &&
            line.back() == '\r')
        {
            line.pop_back();
        }

        std::wstring wideLine =
            Utf8ToWide(line);

        if (!wideLine.empty())
        {
            g_journal.push_back(wideLine);
        }
    }

    if (g_journal.size() > 500)
    {
        g_journal.erase(
            g_journal.begin(),
            g_journal.end() - 500
        );
    }
}


bool JournalMatches(
    const std::wstring& line)
{
    if (g_filter == FILTER_ALL)
    {
        return true;
    }

    if (g_filter == FILTER_CHARGER)
    {
        return line.find(
            L"Зарядное устройство"
        ) != std::wstring::npos;
    }

    if (g_filter == FILTER_CHARGE)
    {
        return line.find(
            L"Уровень заряда"
        ) != std::wstring::npos;
    }

    if (g_filter == FILTER_SLEEP)
    {
        return
            line.find(L"спящий") !=
            std::wstring::npos ||

            line.find(L"гибернац") !=
            std::wstring::npos ||

            line.find(L"Пробуждение") !=
            std::wstring::npos ||

            line.find(L"Возобновление") !=
            std::wstring::npos;
    }

    return true;
}


bool IsAutostartEnabled()
{
    HKEY key = nullptr;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_READ,
        &key
    );

    if (result != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD size = 0;

    result = RegQueryValueExW(
        key,
        L"IIUVM_PowerWidget",
        nullptr,
        &type,
        nullptr,
        &size
    );

    RegCloseKey(key);

    return result == ERROR_SUCCESS;
}


bool SetAutostart(
    bool enabled)
{
    HKEY key = nullptr;

    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_SET_VALUE,
        &key
    );

    if (result != ERROR_SUCCESS)
        return false;

    if (enabled)
    {
        wchar_t path[MAX_PATH]{};

        DWORD length = GetModuleFileNameW(
            nullptr,
            path,
            MAX_PATH
        );

        if (length == 0 ||
            length >= MAX_PATH)
        {
            RegCloseKey(key);
            return false;
        }

        std::wstring command =
            L"\"" +
            std::wstring(path) +
            L"\"";

        result = RegSetValueExW(
            key,
            L"IIUVM_PowerWidget",
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(
                command.c_str()
                ),
            static_cast<DWORD>(
                (command.size() + 1) *
                sizeof(wchar_t)
                )
        );
    }
    else
    {
        result = RegDeleteValueW(
            key,
            L"IIUVM_PowerWidget"
        );

        if (result == ERROR_FILE_NOT_FOUND)
        {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);

    return result == ERROR_SUCCESS;
}


Color BatteryColor(
    BYTE percent)
{
    if (percent == 255)
    {
        return Color(
            180,
            180,
            180
        );
    }

    if (percent <= 20)
    {
        return Color(
            230,
            70,
            70
        );
    }

    if (percent <= 50)
    {
        return Color(
            240,
            190,
            50
        );
    }

    return Color(
        70,
        210,
        110
    );
}


void DrawTextGdi(
    Graphics& graphics,
    const std::wstring& text,
    float x,
    float y,
    float width,
    float height,
    float fontSize,
    const Color& color)
{
    FontFamily family(
        L"Segoe UI"
    );

    Font font(
        &family,
        fontSize,
        FontStyleRegular,
        UnitPixel
    );

    SolidBrush brush(color);

    RectF rect(
        x,
        y,
        width,
        height
    );

    StringFormat format;

    graphics.DrawString(
        text.c_str(),
        -1,
        &font,
        rect,
        &format,
        &brush
    );
}


void DrawMainPage(
    Graphics& graphics,
    int width,
    int height)
{
    graphics.Clear(
        Color(
            245,
            247,
            250
        )
    );

    BatteryInfo battery =
        g_power.GetBatteryInfo();

    DrawTextGdi(
        graphics,
        L"Контроль питания",
        20,
        14,
        static_cast<float>(width - 40),
        30,
        18,
        Color(
            35,
            40,
            50
        )
    );

    float cx = 72.0f;
    float cy = 92.0f;
    float radius = 43.0f;

    Pen backgroundPen(
        Color(
            220,
            225,
            232
        ),
        8
    );

    graphics.DrawArc(
        &backgroundPen,
        cx - radius,
        cy - radius,
        radius * 2,
        radius * 2,
        0,
        360
    );

    if (battery.percent != 255)
    {
        float sweep =
            360.0f *
            static_cast<float>(
                battery.percent
                ) /
            100.0f;

        Pen batteryPen(
            BatteryColor(
                battery.percent
            ),
            8
        );

        graphics.DrawArc(
            &batteryPen,
            cx - radius,
            cy - radius,
            radius * 2,
            radius * 2,
            -90,
            sweep
        );
    }

    std::wstring percentText;

    if (battery.percent == 255)
    {
        percentText = L"--%";
    }
    else
    {
        percentText =
            std::to_wstring(
                battery.percent
            ) +
            L"%";
    }

    DrawTextGdi(
        graphics,
        percentText,
        45,
        80,
        55,
        25,
        14,
        Color(
            35,
            40,
            50
        )
    );

    std::wstring powerText;

    if (battery.connected)
    {
        if (battery.charging)
        {
            powerText = L"Заряжается";
        }
        else
        {
            powerText = L"От сети";
        }
    }
    else
    {
        powerText = L"От батареи";
    }

    DrawTextGdi(
        graphics,
        powerText,
        130,
        55,
        150,
        25,
        13,
        Color(
            45,
            50,
            60
        )
    );

    DrawTextGdi(
        graphics,
        L"Осталось:",
        130,
        82,
        100,
        22,
        11,
        Color(
            120,
            125,
            135
        )
    );

    DrawTextGdi(
        graphics,
        PowerMonitor::FormatTime(
            battery.remainingSeconds
        ),
        130,
        103,
        150,
        25,
        13,
        Color(
            45,
            50,
            60
        )
    );

    DrawTextGdi(
        graphics,
        L"Режим:",
        20,
        145,
        65,
        22,
        11,
        Color(
            120,
            125,
            135
        )
    );

    DrawTextGdi(
        graphics,
        g_power.GetActivePowerSchemeName(),
        75,
        143,
        200,
        25,
        12,
        Color(
            45,
            50,
            60
        )
    );

    DrawTextGdi(
        graphics,
        L"ПКМ — меню",
        20,
        static_cast<float>(height - 30),
        150,
        20,
        10,
        Color(
            140,
            145,
            155
        )
    );
}


void DrawJournalPage(
    Graphics& graphics,
    int width,
    int height)
{
    graphics.Clear(
        Color(
            245,
            247,
            250
        )
    );

    DrawTextGdi(
        graphics,
        L"Полный журнал",
        15,
        10,
        180,
        25,
        17,
        Color(
            35,
            40,
            50
        )
    );

    DrawTextGdi(
        graphics,
        L"[1] Все",
        15,
        43,
        55,
        20,
        9,
        Color(
            50,
            55,
            65
        )
    );

    DrawTextGdi(
        graphics,
        L"[2] Зарядка",
        70,
        43,
        75,
        20,
        9,
        Color(
            50,
            55,
            65
        )
    );

    DrawTextGdi(
        graphics,
        L"[3] Уровень",
        145,
        43,
        75,
        20,
        9,
        Color(
            50,
            55,
            65
        )
    );

    DrawTextGdi(
        graphics,
        L"[4] Сон",
        220,
        43,
        55,
        20,
        9,
        Color(
            50,
            55,
            65
        )
    );

    DrawTextGdi(
        graphics,
        L"[Esc] Назад",
        15,
        static_cast<float>(height - 24),
        100,
        20,
        9,
        Color(
            120,
            125,
            135
        )
    );

    float y = 70.0f;

    for (
        auto it = g_journal.rbegin();
        it != g_journal.rend();
        ++it)
    {
        if (!JournalMatches(*it))
            continue;

        DrawTextGdi(
            graphics,
            *it,
            15,
            y,
            static_cast<float>(width - 30),
            20,
            9,
            Color(
                55,
                60,
                70
            )
        );

        y += 19.0f;

        if (y > height - 40)
            break;
    }
}


void PaintWindow(
    HWND hwnd)
{
    RECT rc{};

    GetClientRect(
        hwnd,
        &rc
    );

    int width =
        rc.right -
        rc.left;

    int height =
        rc.bottom -
        rc.top;

    if (width <= 0 ||
        height <= 0)
    {
        return;
    }

    HDC hdc = GetDC(hwnd);

    if (!hdc)
        return;

    HDC memDC = CreateCompatibleDC(hdc);

    if (!memDC)
    {
        ReleaseDC(
            hwnd,
            hdc
        );

        return;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(
        hdc,
        width,
        height
    );

    if (!bitmap)
    {
        DeleteDC(memDC);

        ReleaseDC(
            hwnd,
            hdc
        );

        return;
    }

    HBITMAP oldBitmap =
        static_cast<HBITMAP>(
            SelectObject(
                memDC,
                bitmap
            )
            );

    Graphics graphics(memDC);

    graphics.SetSmoothingMode(
        SmoothingModeAntiAlias
    );

    if (g_showJournal)
    {
        DrawJournalPage(
            graphics,
            width,
            height
        );
    }
    else
    {
        DrawMainPage(
            graphics,
            width,
            height
        );
    }

    BitBlt(
        hdc,
        0,
        0,
        width,
        height,
        memDC,
        0,
        0,
        SRCCOPY
    );

    SelectObject(
        memDC,
        oldBitmap
    );

    DeleteObject(bitmap);
    DeleteDC(memDC);

    ReleaseDC(
        hwnd,
        hdc
    );
}


void ShowContextMenu(
    HWND hwnd,
    int x,
    int y)
{
    HMENU menu = CreatePopupMenu();

    if (!menu)
        return;

    AppendMenuW(
        menu,
        MF_STRING,
        1001,
        L"Спящий режим"
    );

    AppendMenuW(
        menu,
        MF_STRING,
        1002,
        L"Гибернация"
    );

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr
    );

    AppendMenuW(
        menu,
        MF_STRING,
        1003,
        L"Показать полный журнал"
    );

    HMENU filterMenu = CreatePopupMenu();

    if (filterMenu)
    {
        AppendMenuW(
            filterMenu,
            MF_STRING,
            1010,
            L"Все события"
        );

        AppendMenuW(
            filterMenu,
            MF_STRING,
            1011,
            L"Только подключение зарядки"
        );

        AppendMenuW(
            filterMenu,
            MF_STRING,
            1012,
            L"Только изменение заряда"
        );

        AppendMenuW(
            filterMenu,
            MF_STRING,
            1013,
            L"Только переходы сна"
        );

        AppendMenuW(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(
                filterMenu
                ),
            L"Фильтр журнала"
        );
    }

    HMENU schemeMenu = CreatePopupMenu();

    auto schemes =
        g_power.GetPowerSchemes();

    if (schemeMenu)
    {
        for (
            size_t i = 0;
            i < schemes.size();
            ++i)
        {
            AppendMenuW(
                schemeMenu,
                MF_STRING,
                1100 + static_cast<UINT>(i),
                schemes[i].name.c_str()
            );
        }

        AppendMenuW(
            menu,
            MF_POPUP,
            reinterpret_cast<UINT_PTR>(
                schemeMenu
                ),
            L"Режим питания"
        );
    }

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr
    );

    bool autostart =
        IsAutostartEnabled();

    AppendMenuW(
        menu,
        MF_STRING |
        (autostart
            ? MF_CHECKED
            : 0),
        1200,
        L"Автозапуск Windows"
    );

    AppendMenuW(
        menu,
        MF_SEPARATOR,
        0,
        nullptr
    );

    AppendMenuW(
        menu,
        MF_STRING,
        1201,
        L"Выход"
    );

    SetForegroundWindow(hwnd);

    UINT command = TrackPopupMenu(
        menu,
        TPM_RETURNCMD |
        TPM_NONOTIFY,
        x,
        y,
        0,
        hwnd,
        nullptr
    );

    if (command == 1001)
    {
        if (!g_power.Sleep())
        {
            std::wstring error =
                g_power.GetLastErrorText();

            AddJournal(
                L"Не удалось перейти в спящий режим: " +
                error
            );

            MessageBoxW(
                hwnd,
                error.c_str(),
                L"Ошибка спящего режима",
                MB_OK |
                MB_ICONERROR
            );
        }
    }
    else if (command == 1002)
    {
        if (!g_power.Hibernate())
        {
            std::wstring error =
                g_power.GetLastErrorText();

            AddJournal(
                L"Не удалось перейти в гибернацию: " +
                error
            );

            MessageBoxW(
                hwnd,
                error.c_str(),
                L"Ошибка гибернации",
                MB_OK |
                MB_ICONERROR
            );
        }
    }
    else if (command == 1003)
    {
        g_showJournal = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (command == 1010)
    {
        g_filter = FILTER_ALL;
        g_showJournal = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (command == 1011)
    {
        g_filter = FILTER_CHARGER;
        g_showJournal = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (command == 1012)
    {
        g_filter = FILTER_CHARGE;
        g_showJournal = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (command == 1013)
    {
        g_filter = FILTER_SLEEP;
        g_showJournal = true;

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (
        command >= 1100 &&
        command <
        1100 + schemes.size())
    {
        size_t index =
            command - 1100;

        if (g_power.SetActivePowerScheme(
            schemes[index].guid))
        {
            AddJournal(
                L"Изменена схема питания: " +
                schemes[index].name
            );
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (command == 1200)
    {
        bool current =
            IsAutostartEnabled();

        if (!SetAutostart(!current))
        {
            MessageBoxW(
                hwnd,
                L"Не удалось изменить автозапуск.",
                L"Ошибка",
                MB_OK |
                MB_ICONERROR
            );
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );
    }
    else if (command == 1201)
    {
        DestroyWindow(hwnd);
    }

    DestroyMenu(menu);
}


void CheckBatteryChanges(
    HWND hwnd)
{
    BatteryInfo current =
        g_power.GetBatteryInfo();

    if (g_firstBatteryCheck)
    {
        g_lastBattery = current;
        g_firstBatteryCheck = false;
        return;
    }

    if (current.connected !=
        g_lastBattery.connected)
    {
        if (current.connected)
        {
            AddJournal(
                L"Зарядное устройство подключено"
            );
        }
        else
        {
            AddJournal(
                L"Зарядное устройство отключено"
            );
        }
    }

    if (current.percent !=
        g_lastBattery.percent)
    {
        if (current.percent != 255)
        {
            AddJournal(
                L"Уровень заряда: " +
                std::to_wstring(
                    current.percent
                ) +
                L"%"
            );
        }
    }

    g_lastBattery = current;

    InvalidateRect(
        hwnd,
        nullptr,
        FALSE
    );
}


LRESULT CALLBACK WndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        SetTimer(
            hwnd,
            1,
            1000,
            nullptr
        );

        return 0;
    }

    case WM_TIMER:
    {
        CheckBatteryChanges(hwnd);
        return 0;
    }

    case WM_POWERBROADCAST:
    {
        switch (wParam)
        {
        case PBT_APMSUSPEND:
        {
            AddJournal(
                L"Переход в спящий режим"
            );

            break;
        }

        case PBT_APMRESUMEAUTOMATIC:
        {
            AddJournal(
                L"Пробуждение системы"
            );

            break;
        }

        case PBT_APMRESUMESUSPEND:
        {
            AddJournal(
                L"Возобновление работы после сна"
            );

            break;
        }

        case PBT_APMPOWERSTATUSCHANGE:
        {
            AddJournal(
                L"Изменилось состояние питания"
            );

            break;
        }
        }

        InvalidateRect(
            hwnd,
            nullptr,
            FALSE
        );

        return TRUE;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};

        BeginPaint(
            hwnd,
            &ps
        );

        PaintWindow(hwnd);

        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }

    case WM_RBUTTONUP:
    {
        POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };

        ClientToScreen(
            hwnd,
            &point
        );

        ShowContextMenu(
            hwnd,
            point.x,
            point.y
        );

        return 0;
    }

    // Правый клик по HTCAPTION приходит как WM_NCRBUTTONUP.
    case WM_NCRBUTTONUP:
    {
        ShowContextMenu(
            hwnd,
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        );

        return 0;
    }

    case WM_NCHITTEST:
    {
        POINT point{
            GET_X_LPARAM(lParam),
            GET_Y_LPARAM(lParam)
        };

        ScreenToClient(
            hwnd,
            &point
        );

        // Фильтры полного журнала остаются кликабельными.
        if (g_showJournal &&
            point.y >= 40 &&
            point.y <= 65)
        {
            return HTCLIENT;
        }

        // Остальная область окна предназначена для перетаскивания.
        return HTCAPTION;
    }

    case WM_LBUTTONDOWN:
    {
        if (g_showJournal)
        {
            int y = GET_Y_LPARAM(lParam);

            if (y >= 40 &&
                y <= 65)
            {
                int x = GET_X_LPARAM(lParam);

                if (x < 70)
                {
                    g_filter = FILTER_ALL;
                }
                else if (x < 145)
                {
                    g_filter = FILTER_CHARGER;
                }
                else if (x < 220)
                {
                    g_filter = FILTER_CHARGE;
                }
                else
                {
                    g_filter = FILTER_SLEEP;
                }

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }
        }

        break;
    }

    case WM_KEYDOWN:
    {
        if (g_showJournal)
        {
            if (wParam == VK_ESCAPE)
            {
                g_showJournal = false;

                InvalidateRect(
                    hwnd,
                    nullptr,
                    FALSE
                );

                return 0;
            }

            if (wParam == '1')
            {
                g_filter = FILTER_ALL;
            }
            else if (wParam == '2')
            {
                g_filter = FILTER_CHARGER;
            }
            else if (wParam == '3')
            {
                g_filter = FILTER_CHARGE;
            }
            else if (wParam == '4')
            {
                g_filter = FILTER_SLEEP;
            }

            InvalidateRect(
                hwnd,
                nullptr,
                FALSE
            );

            return 0;
        }

        break;
    }

    case WM_DESTROY:
    {
        KillTimer(
            hwnd,
            1
        );

        PostQuitMessage(0);

        return 0;
    }
    }

    return DefWindowProcW(
        hwnd,
        message,
        wParam,
        lParam
    );
}


int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE,
    PWSTR,
    int)
{
    GdiplusStartupInput gdiplusStartupInput;

    if (GdiplusStartup(
        &g_gdiplusToken,
        &gdiplusStartupInput,
        nullptr) != Ok)
    {
        MessageBoxW(
            nullptr,
            L"Не удалось запустить GDI+.",
            L"Ошибка",
            MB_OK |
            MB_ICONERROR
        );

        return 1;
    }

    const wchar_t CLASS_NAME[] =
        L"IIUVM_PowerWidget";

    WNDCLASSW wc{};

    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;

    wc.hCursor = LoadCursorW(
        nullptr,
        IDC_ARROW
    );

    wc.hbrBackground = nullptr;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    HANDLE mutex = CreateMutexW(
        nullptr,
        TRUE,
        L"IIUVM_PowerWidget_Mutex"
    );

    if (
        mutex != nullptr &&
        GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(mutex);

        GdiplusShutdown(
            g_gdiplusToken
        );

        return 0;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        CLASS_NAME,
        L"Контроль питания",
        WS_POPUP,
        300,
        200,
        300,
        200,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd)
    {
        if (mutex)
        {
            CloseHandle(mutex);
        }

        GdiplusShutdown(
            g_gdiplusToken
        );

        MessageBoxW(
            nullptr,
            L"Не удалось создать окно.",
            L"Ошибка",
            MB_OK |
            MB_ICONERROR
        );

        return 1;
    }

    GUID settings[] =
    {
        GUID_ACDC_POWER_SOURCE,
        GUID_BATTERY_PERCENTAGE_REMAINING,
        GUID_POWERSCHEME_PERSONALITY
    };

    for (const GUID& guid : settings)
    {
        RegisterPowerSettingNotification(
            hwnd,
            &guid,
            DEVICE_NOTIFY_WINDOW_HANDLE
        );
    }

    LoadJournal();

    AddJournal(
        L"Программа запущена"
    );

    ShowWindow(
        hwnd,
        SW_SHOW
    );

    UpdateWindow(hwnd);

    MSG msg{};

    while (GetMessageW(
        &msg,
        nullptr,
        0,
        0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex)
    {
        CloseHandle(mutex);
    }

    GdiplusShutdown(
        g_gdiplusToken
    );

    return 0;
}