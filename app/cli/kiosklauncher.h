#pragma once

#include <QObject>
#include <QVariant>

class ComputerManager;
class NvComputer;
class Session;
class StreamingPreferences;

namespace CliKiosk
{

class Launcher : public QObject
{
    Q_OBJECT

public:
    explicit Launcher(QString computer, QObject *parent = nullptr);
    ~Launcher();
    Q_INVOKABLE void execute(ComputerManager *manager);
    Q_INVOKABLE bool isExecuted() const;
    Q_INVOKABLE int getComputerIndex() const;

signals:
    void searchingComputer();
    void computerReady(int computerIndex);
    void failed(QString text);

private slots:
    void onComputerFound(NvComputer *computer);
    void onTimeout();

private:
    QString m_ComputerName;
    ComputerManager *m_ComputerManager;
    NvComputer *m_Computer;
    bool m_Executed;
    int m_ComputerIndex;
};

}
