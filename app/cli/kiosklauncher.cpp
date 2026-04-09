#include "kiosklauncher.h"

namespace CliKiosk
{

Launcher::Launcher(QString district, QString venue, QObject *parent)
    : QObject(parent),
      m_District(district),
      m_Venue(venue)
{
}

Launcher::~Launcher()
{
}

QString Launcher::getDistrict() const
{
    return m_District;
}

QString Launcher::getVenue() const
{
    return m_Venue;
}

}
