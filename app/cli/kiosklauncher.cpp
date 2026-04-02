#include "kiosklauncher.h"
#include "backend/computermanager.h"
#include "backend/computerseeker.h"

#include <QCoreApplication>

#define COMPUTER_SEEK_TIMEOUT 30000

namespace CliKiosk
{

Launcher::Launcher(QString computer, QObject *parent)
    : QObject(parent),
      m_ComputerName(computer),
      m_ComputerManager(nullptr),
      m_Computer(nullptr),
      m_Executed(false),
      m_ComputerIndex(-1)
{
}

Launcher::~Launcher()
{
}

void Launcher::execute(ComputerManager *manager)
{
    if (m_Executed) {
        return;
    }
    m_Executed = true;
    m_ComputerManager = manager;

    auto seeker = new ComputerSeeker(m_ComputerManager, m_ComputerName, this);
    connect(seeker, &ComputerSeeker::computerFound,
            this, &Launcher::onComputerFound);
    connect(seeker, &ComputerSeeker::errorTimeout,
            this, &Launcher::onTimeout);
    seeker->start(COMPUTER_SEEK_TIMEOUT);

    emit searchingComputer();
}

bool Launcher::isExecuted() const
{
    return m_Executed;
}

int Launcher::getComputerIndex() const
{
    return m_ComputerIndex;
}

void Launcher::onComputerFound(NvComputer *computer)
{
    if (computer->pairState != NvComputer::PS_PAIRED) {
        emit failed(QObject::tr("Computer %1 has not been paired. "
                                "Please open Moonlight to pair before streaming.")
                    .arg(computer->name));
        return;
    }

    m_Computer = computer;

    // Find the computer's index in ComputerManager for AppModel
    QVector<NvComputer*> computers = m_ComputerManager->getComputers();
    for (int i = 0; i < computers.size(); i++) {
        if (computers[i] == computer) {
            m_ComputerIndex = i;
            break;
        }
    }

    emit computerReady(m_ComputerIndex);
}

void Launcher::onTimeout()
{
    emit failed(QObject::tr("Failed to connect to %1").arg(m_ComputerName));
}

}
