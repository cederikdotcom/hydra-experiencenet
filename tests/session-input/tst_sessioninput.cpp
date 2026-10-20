#include <QtTest>
#include <QScopeGuard>
#include "../../app/api/sessioninput.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif

class SessionInputTest : public QObject
{
    Q_OBJECT
private slots:
    void ignoresKeepAwakeButCountsInput()
    {
        SessionInput input;
        quint64 idle = 0, ignored = 0, events = 0;
        quint32 session = 0;
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(input.sample(idle, ignored, events, session), 5000);
        auto beforeIdle = idle;
        INPUT move = {};
        move.type = INPUT_MOUSE;
        move.mi.dwFlags = MOUSEEVENTF_MOVE;
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(SendInput(1, &move, sizeof(move)), UINT(1));
            QTest::qWait(50);
        }
        QVERIFY(input.sample(idle, ignored, events, session));
        // Windows may elide a zero-motion hook notification entirely. The
        // behavior we need is a growing idle age, not a particular event count.
        QVERIFY2(idle >= beforeIdle + 150, "synthetic keep-awake reset the activity timer");

        // Remote/injected keyboard input remains real activity. F15 has no
        // default desktop action and the key is released in the same batch.
        auto beforeEvents = events;
        INPUT keys[2] = {};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = VK_F15;
        keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
        QCOMPARE(SendInput(2, keys, sizeof(INPUT)), UINT(2));
        QTRY_VERIFY(input.sample(idle, ignored, events, session) && events >= beforeEvents + 2);
        QVERIFY(idle < 1000);

        // Synthetic motion with displacement (e.g. a remote player) counts.
        POINT position;
        QVERIFY(GetPhysicalCursorPos(&position));
        auto restoreCursor = qScopeGuard([position] { SetPhysicalCursorPos(position.x, position.y); });
        // A one-unit relative move can round to zero under pointer scaling.
        // Use a visibly different absolute target for the movement assertion.
        move.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        move.mi.dx = position.x > GetSystemMetrics(SM_CXSCREEN) / 2 ? 0 : 65535;
        move.mi.dy = position.y > GetSystemMetrics(SM_CYSCREEN) / 2 ? 0 : 65535;
        beforeEvents = events;
        QCOMPARE(SendInput(1, &move, sizeof(move)), UINT(1));
        QTest::qWait(100);
        POINT after;
        QVERIFY(GetPhysicalCursorPos(&after));
        input.sample(idle, ignored, events, session);
        qInfo("mouse before=(%ld,%ld) after=(%ld,%ld) events=%llu ignored=%llu",
              position.x, position.y, after.x, after.y, events, ignored);
        QTRY_VERIFY(input.sample(idle, ignored, events, session) && events > beforeEvents);
#else
        QVERIFY(!input.sample(idle, ignored, events, session));
#endif
    }
};

QTEST_GUILESS_MAIN(SessionInputTest)
#include "tst_sessioninput.moc"
