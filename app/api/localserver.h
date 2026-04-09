#pragma once

#include <QObject>
#include <QTcpServer>

// Lightweight HTTP server on 127.0.0.1:9741 exposing view-layer
// endpoints for the hydraheadflatscreen Go service to proxy.
//
// Endpoints:
//   GET /api/v1/screenshot - captures the screen and returns JPEG
class LocalServer : public QObject
{
    Q_OBJECT

public:
    explicit LocalServer(QObject* parent = nullptr);
    ~LocalServer();

    bool start(quint16 port = 9741);

private slots:
    void handleConnection();

private:
    void handleRequest(QTcpSocket* socket, const QByteArray& request);
    void handleScreenshot(QTcpSocket* socket);
    void sendResponse(QTcpSocket* socket, int statusCode, const QByteArray& contentType, const QByteArray& body);
    void sendError(QTcpSocket* socket, int statusCode, const QString& message);

    QTcpServer* m_server;
};
