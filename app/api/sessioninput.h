#pragma once

#include <QtGlobal>

// Input telemetry only. HydraHead owns the inactivity timeout and exit action.
class SessionInput
{
public:
    SessionInput();
    ~SessionInput();
    SessionInput(const SessionInput&) = delete;
    SessionInput& operator=(const SessionInput&) = delete;

    bool sample(quint64& idleMs, quint64& ignoredKeepAwake,
                quint64& activityEvents, quint32& sessionId) const;

private:
    class Worker;
    Worker* m_worker;
};
