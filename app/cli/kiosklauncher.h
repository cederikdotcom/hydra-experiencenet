#pragma once

#include <QObject>
#include <QString>

namespace CliKiosk
{

class Launcher : public QObject
{
    Q_OBJECT

public:
    explicit Launcher(QString district, QString venue, QObject *parent = nullptr);
    ~Launcher();
    Q_INVOKABLE QString getDistrict() const;
    Q_INVOKABLE QString getVenue() const;

signals:
    void ready();

private:
    QString m_District;
    QString m_Venue;
};

}
