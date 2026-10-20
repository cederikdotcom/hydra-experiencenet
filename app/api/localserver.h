#pragma once

#include <QObject>
#include <QTcpServer>
#include "sessioninput.h"

// Lightweight HTTP server on 127.0.0.1:9741 exposing view-layer
// endpoints for the hydraheadflatscreen Go service to proxy.
//
// Endpoints:
//   GET /api/v1/probe?host=IP&port=PORT - TCP connectivity check (routes through app's TCC)
//   POST /api/v1/window/hide - hide the kiosk window (stream takes over display)
//   POST /api/v1/window/show - show the kiosk window (stream ended)
//   POST /api/v1/overlay/show - raise the exit overlay on top of an agent-initiated stream
//   GET /api/v1/input/idle - session input age; HydraHead owns timeout policy
class LocalServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalServer(QObject* parent = nullptr);
    ~LocalServer();

    bool start(quint16 port = 9741);

signals:
    void overlayShowRequested();
    void kioskWindowShown();

private slots:
    void handleConnection();

private:
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void handleProbe(QTcpSocket* socket, const QByteArray& path);
    void handleWindowHide(QTcpSocket* socket);
    void handleWindowShow(QTcpSocket* socket);
    void handleOverlayShow(QTcpSocket* socket);
    void handleInputIdle(QTcpSocket* socket);
    void sendResponse(QTcpSocket* socket, int statusCode, const QByteArray& contentType, const QByteArray& body);
    void sendError(QTcpSocket* socket, int statusCode, const QString& message);

    QTcpServer* m_server;
    SessionInput m_input;
};
