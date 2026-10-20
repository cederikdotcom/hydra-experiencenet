#include "sessioninput.h"

#ifdef Q_OS_WIN
#include <QThread>
#include <atomic>
#include <windows.h>

class SessionInput::Worker : public QThread
{
public:
    std::atomic<quint64> lastActivity{0}, ignored{0}, events{0}, heartbeat{0};
    std::atomic<DWORD> threadId{0};
    std::atomic<bool> ready{false};

    ~Worker() override
    {
        requestInterruption();
        if (DWORD id = threadId.load()) {
            PostThreadMessageW(id, WM_QUIT, 0, 0);
        }
        wait();
    }

protected:
    void run() override
    {
        // Separate message pump: synchronous UI/network work must never stall
        // a low-level hook and make Windows silently remove it.
        MSG msg;
        PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
        threadId.store(GetCurrentThreadId());
        current = this;
        // Low-level mouse packets use physical/per-monitor coordinates.
        havePoint = GetPhysicalCursorPos(&point);
        lastActivity.store(GetTickCount64());
        heartbeat.store(GetTickCount64());
        HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardInput, GetModuleHandleW(nullptr), 0);
        HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseInput, GetModuleHandleW(nullptr), 0);
        UINT_PTR timer = SetTimer(nullptr, 0, 1000, nullptr);
        ready.store(keyboard && mouse && timer);
        if (ready.load()) {
            while (!isInterruptionRequested() && GetMessageW(&msg, nullptr, 0, 0) > 0) {
                heartbeat.store(GetTickCount64());
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        ready.store(false);
        if (timer) KillTimer(nullptr, timer);
        if (mouse) UnhookWindowsHookEx(mouse);
        if (keyboard) UnhookWindowsHookEx(keyboard);
        current = nullptr;
    }

private:
    static thread_local Worker* current;
    POINT point{};
    bool havePoint = false;

    void activity()
    {
        lastActivity.store(GetTickCount64());
        events.fetch_add(1);
    }

    static LRESULT CALLBACK keyboardInput(int code, WPARAM message, LPARAM data)
    {
        if (code == HC_ACTION && current) current->activity();
        return CallNextHookEx(nullptr, code, message, data);
    }

    static LRESULT CALLBACK mouseInput(int code, WPARAM message, LPARAM data)
    {
        if (code == HC_ACTION && current) {
            const auto* input = reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
            // Unreal's screensaver inhibitor injects a zero-pixel mouse move
            // every 50 seconds. GetLastInputInfo counts that as player input.
            // Ignore only injected moves with no displacement. Real hardware
            // movement, clicks, wheels, and remote moving input still count.
            bool keepAwake = message == WM_MOUSEMOVE &&
                (input->flags & LLMHF_INJECTED) && current->havePoint &&
                input->pt.x == current->point.x && input->pt.y == current->point.y;
            current->point = input->pt;
            current->havePoint = true;
            if (keepAwake) current->ignored.fetch_add(1);
            else current->activity();
        }
        // Observe only: never swallow or alter input.
        return CallNextHookEx(nullptr, code, message, data);
    }
};

thread_local SessionInput::Worker* SessionInput::Worker::current = nullptr;
#else
class SessionInput::Worker {};
#endif

SessionInput::SessionInput() : m_worker(nullptr)
{
#ifdef Q_OS_WIN
    m_worker = new Worker;
    m_worker->start();
#endif
}

SessionInput::~SessionInput()
{
    delete m_worker;
}

bool SessionInput::sample(quint64& idleMs, quint64& ignoredKeepAwake,
                          quint64& activityEvents, quint32& sessionId) const
{
#ifdef Q_OS_WIN
    DWORD session;
    quint64 heartbeat = m_worker->heartbeat.load();
    quint64 now = GetTickCount64();
    if (!m_worker->ready.load() || !m_worker->isRunning() ||
        now - heartbeat > 3000 ||
        !ProcessIdToSessionId(GetCurrentProcessId(), &session)) return false;
    // An input callback may run between the clock reads.
    quint64 last = m_worker->lastActivity.load();
    idleMs = now >= last ? now - last : 0;
    for (int key = 1; key < 256; ++key) {
        if (GetAsyncKeyState(key) & 0x8000) {
            idleMs = 0;
            break;
        }
    }
    ignoredKeepAwake = m_worker->ignored.load();
    activityEvents = m_worker->events.load();
    sessionId = session;
    return true;
#else
    Q_UNUSED(idleMs)
    Q_UNUSED(ignoredKeepAwake)
    Q_UNUSED(activityEvents)
    Q_UNUSED(sessionId)
    return false;
#endif
}
