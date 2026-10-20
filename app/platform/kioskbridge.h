#pragma once

#include <QObject>
#include <QWindow>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <QTimer>

#ifdef __APPLE__
#include "macos_permissions.h"
#endif

// Small QObject exposed as a QML singleton to let QML code invoke the
// platform-specific kiosk helpers without each caller having to thread
// a C++ object through. Currently only used to ask the floating Qt exit
// overlay to follow all macOS Spaces once its Window is constructed.
class KioskBridge : public QObject
{
    Q_OBJECT

public:
    explicit KioskBridge(QObject* parent = nullptr) : QObject(parent),
        m_PinRequired(QFileInfo::exists(pinPath())) {}

    // Operator-managed per-installation artwork remains available offline.
    Q_INVOKABLE QUrl experienceArtwork(const QString& name) const
    {
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(name).hasMatch())
            return QUrl();
        const QString path = QCoreApplication::applicationDirPath() +
            QStringLiteral("/artwork/") + name + QStringLiteral(".jpg");
        return QFileInfo(path).isFile() ? QUrl::fromLocalFile(path) : QUrl();
    }

    // Provisioned by the operator, outside the application package. Cache the
    // enabled state so deleting/unreading the file cannot unlock a live kiosk.
    Q_INVOKABLE bool operatorPinRequired() const { return m_PinRequired; }
    Q_INVOKABLE bool verifyOperatorPin(const QString& pin)
    {
        if (!m_PinRequired) return false;
        if (m_LastAttempt.isValid() && m_LastAttempt.elapsed() < 2000) return false;
        m_LastAttempt.start();
        QFile file(pinPath());
        if (!file.open(QIODevice::ReadOnly)) return false;
        const QByteArray expected = file.read(256).trimmed().toLower();
        if (expected.size() != 64 ||
            !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                 .match(QString::fromLatin1(expected)).hasMatch() ||
            !QRegularExpression(QStringLiteral("^[0-9]{4,12}$")).match(pin).hasMatch())
            return false;
        const QByteArray actual = QCryptographicHash::hash(pin.toUtf8(),
                                                          QCryptographicHash::Sha256).toHex();
        unsigned char difference = 0;
        for (int i = 0; i < actual.size(); ++i) difference |= actual[i] ^ expected[i];
        return difference == 0;
    }

    // Called from StreamOverlay.qml's Component.onCompleted, passing
    // the overlay Window itself. Grabs the native winId (an NSView* on
    // macOS) and asks the platform helper to set collectionBehavior so
    // the window appears on every Space, including a fullscreen Space
    // that belongs to the kiosk or stream window.
    Q_INVOKABLE void makeFollowAllSpaces(QWindow* window)
    {
        if (window == nullptr) {
            return;
        }
#ifdef __APPLE__
        WId id = window->winId();
        makeWindowFollowAllSpaces(static_cast<uint64_t>(id));
#else
        Q_UNUSED(window);
#endif
    }

    // Main kiosk window only. Native Space changes can move AppKit's frame
    // after showNormal/setGeometry have returned; keep the actual frame fitted
    // while visible, and stop checking while the game owns the display.
    Q_INVOKABLE void coverFullDisplay(QWindow* window)
    {
#ifdef __APPLE__
        if (window == nullptr) return;
        const auto fit = [window]() {
            if (window->isVisible())
                fitKioskWindowToScreen(static_cast<uint64_t>(window->winId()));
        };
        if (!window->findChild<QTimer*>(QStringLiteral("hydraKioskFrameGuard"),
                                       Qt::FindDirectChildrenOnly)) {
            QTimer* timer = new QTimer(window);
            timer->setObjectName(QStringLiteral("hydraKioskFrameGuard"));
            timer->setInterval(100);
            QObject::connect(timer, &QTimer::timeout, window, fit);
            QObject::connect(window, &QWindow::xChanged, window, [fit](int) { fit(); });
            QObject::connect(window, &QWindow::yChanged, window, [fit](int) { fit(); });
            QObject::connect(window, &QWindow::widthChanged, window, [fit](int) { fit(); });
            QObject::connect(window, &QWindow::heightChanged, window, [fit](int) { fit(); });
            QObject::connect(window, &QWindow::visibleChanged, window,
                             [timer, fit](bool visible) {
                if (visible) {
                    fit();
                    timer->start();
                    QTimer::singleShot(0, timer, fit);
                } else {
                    timer->stop();
                }
            });
            if (window->isVisible()) timer->start();
        }
        fit();
#else
        Q_UNUSED(window);
#endif
    }
private:
    static QString pinPath() {
        return QCoreApplication::applicationDirPath() + QStringLiteral("/operator-pin.sha256");
    }
    const bool m_PinRequired;
    QElapsedTimer m_LastAttempt;
};
