#include "pch.h"
#include "Platform/ScrollDriver.h"

#include "Platform/ScreenCapture.h"

#include <UIAutomation.h>

#include <cmath>
#include <vector>

#pragma comment(lib, "user32.lib")

namespace Eikon::ScrollDriver
{
    namespace
    {
        void SendKey(WORD key, int count)
        {
            std::vector<INPUT> inputs;
            for (int i = 0; i < count; ++i)
            {
                INPUT down{};
                down.type = INPUT_KEYBOARD;
                down.ki.wVk = key;
                down.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
                INPUT up = down;
                up.ki.dwFlags |= KEYEVENTF_KEYUP;
                inputs.push_back(down);
                inputs.push_back(up);
            }
            if (!inputs.empty())
            {
                SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
            }
        }

        bool ScrollWithAutomation(POINT point, int direction, int count)
        {
            winrt::com_ptr<IUIAutomation> automation;
            if (FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void())))
            {
                return false;
            }
            winrt::com_ptr<IUIAutomationElement> element;
            if (FAILED(automation->ElementFromPoint(point, element.put())) || !element)
            {
                return false;
            }
            winrt::com_ptr<IUIAutomationTreeWalker> walker;
            automation->get_ControlViewWalker(walker.put());
            for (int depth = 0; element && depth < 32; ++depth)
            {
                winrt::com_ptr<IUIAutomationScrollPattern> scroll;
                if (SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollPatternId, __uuidof(IUIAutomationScrollPattern), scroll.put_void())) && scroll)
                {
                    BOOL vertical = FALSE;
                    if (SUCCEEDED(scroll->get_CurrentVerticallyScrollable(&vertical)) && vertical)
                    {
                        // Never LargeIncrement (a page) — the matchers need overlap.
                        ScrollAmount const amount = direction < 0 ? ScrollAmount_SmallIncrement : ScrollAmount_SmallDecrement;
                        for (int i = 0; i < count; ++i)
                        {
                            if (FAILED(scroll->Scroll(ScrollAmount_NoAmount, amount)))
                            {
                                return false;
                            }
                        }
                        return true;
                    }
                }
                if (!walker)
                {
                    break;
                }
                winrt::com_ptr<IUIAutomationElement> parent;
                if (FAILED(walker->GetParentElement(element.get(), parent.put())))
                {
                    break;
                }
                element = std::move(parent);
            }
            return false;
        }
    }

    std::optional<POINT> CursorPosition()
    {
        POINT point{};
        if (!GetCursorPos(&point))
        {
            return std::nullopt;
        }
        return point;
    }

    void MoveCursor(POINT point)
    {
        SetCursorPos(point.x, point.y);
    }

    POINT ParkingPoint()
    {
        // Width - 2, 100: right edge of the main display, 100 pt from its top.
        auto const primary = ScreenCapture::PrimaryMonitor();
        if (!primary)
        {
            return { GetSystemMetrics(SM_CXSCREEN) - 2, 100 };
        }
        return { primary->bounds.right - 2, primary->bounds.top + static_cast<LONG>(std::lround(100 * primary->scale)) };
    }

    void PostWheel(int direction, int notches, bool reverse)
    {
        int const sign = (direction < 0 ? -1 : 1) * (reverse ? -1 : 1);
        std::vector<INPUT> inputs(static_cast<std::size_t>((std::max)(notches, 0)));
        for (auto& input : inputs)
        {
            input.type = INPUT_MOUSE;
            input.mi.mouseData = static_cast<DWORD>(sign * WHEEL_DELTA);
            input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        }
        if (!inputs.empty())
        {
            SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        }
    }

    void PostAutomationScroll(POINT point, int direction, int count)
    {
        if (!ScrollWithAutomation(point, direction, count))
        {
            SendKey(static_cast<WORD>(direction < 0 ? VK_DOWN : VK_UP), count);
        }
    }

    namespace
    {
        // The monitor whose hooks run on this thread.
        thread_local InputMonitor* t_monitor = nullptr;
    }

    InputMonitor::InputMonitor(Trigger trigger)
        : m_trigger(trigger)
    {
        auto ready = std::shared_ptr<void>{ CreateEventW(nullptr, TRUE, FALSE, nullptr), [](void* handle) { CloseHandle(static_cast<HANDLE>(handle)); } };
        if (!ready)
        {
            return;
        }
        m_thread = std::thread([this, ready] {
            t_monitor = this;
            MSG message{};
            PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // create the queue first
            m_threadId = GetCurrentThreadId();
            HINSTANCE const instance = GetModuleHandleW(nullptr);
            HHOOK const keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, &KeyboardProc, instance, 0);
            HHOOK const mouse = m_trigger == Trigger::AnyKeyOrClick ? SetWindowsHookExW(WH_MOUSE_LL, &MouseProc, instance, 0) : nullptr;
            m_started = keyboard != nullptr;
            SetEvent(static_cast<HANDLE>(ready.get()));
            if (keyboard)
            {
                while (!m_stop.load(std::memory_order_acquire) && GetMessageW(&message, nullptr, 0, 0) > 0)
                {
                    if (message.message == WM_QUIT)
                    {
                        break;
                    }
                    DispatchMessageW(&message);
                }
            }
            if (keyboard)
            {
                UnhookWindowsHookEx(keyboard);
            }
            if (mouse)
            {
                UnhookWindowsHookEx(mouse);
            }
            t_monitor = nullptr;
        });
        DWORD const wait = WaitForSingleObject(static_cast<HANDLE>(ready.get()), 2000);
        if (wait != WAIT_OBJECT_0)
        {
            // Startup failed or was delayed. Ask the queue to leave; the thread has created it before
            // publishing m_threadId, so do not post until that publication is visible.
            m_stop = true;
            DWORD const id = m_threadId;
            if (id)
            {
                PostThreadMessageW(id, WM_QUIT, 0, 0);
            }
        }
    }

    InputMonitor::~InputMonitor()
    {
        m_stop = true;
        if (m_thread.joinable())
        {
            DWORD const id = m_threadId;
            if (m_started.load(std::memory_order_acquire) && id)
            {
                PostThreadMessageW(id, WM_QUIT, 0, 0);
            }
            m_thread.join();
        }
    }

    LRESULT CALLBACK InputMonitor::MouseProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && t_monitor && (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN))
        {
            auto const* info = reinterpret_cast<MSLLHOOKSTRUCT const*>(lParam);
            if (!(info->flags & LLMHF_INJECTED))
            {
                t_monitor->m_triggered = true;
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    LRESULT CALLBACK InputMonitor::KeyboardProc(int code, WPARAM wParam, LPARAM lParam)
    {
        if (code == HC_ACTION && t_monitor && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
        {
            auto const* info = reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
            // Auto-scroll ignores injected keys (its own VK_DOWN fallback); manual mode injects
            // nothing, so any Escape counts.
            bool const fires = t_monitor->m_trigger == Trigger::AnyKeyOrClick ? !(info->flags & LLKHF_INJECTED) : info->vkCode == VK_ESCAPE;
            if (fires)
            {
                t_monitor->m_triggered = true;
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }
}
