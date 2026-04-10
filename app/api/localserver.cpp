#include "localserver.h"

#include <QTcpSocket>
#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QProcess>
#include <QGuiApplication>
#include <QWindow>
#include <SDL_log.h>


LocalServer::LocalServer(QObject* parent)
    : QObject(parent),
      m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection,
            this, &LocalServer::handleConnection);
}

LocalServer::~LocalServer()
{
    m_server->close();
}

bool LocalServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "LocalServer: failed to listen on port %d: %s",
                     port, m_server->errorString().toUtf8().constData());
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "LocalServer: listening on 127.0.0.1:%d", port);
    return true;
}

void LocalServer::handleConnection()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray request = socket->readAll();
            handleRequest(socket, request);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);

        // Handle case where data arrived before signal connection
        if (socket->bytesAvailable() > 0) {
            QByteArray request = socket->readAll();
            handleRequest(socket, request);
        }
    }
}

void LocalServer::handleRequest(QTcpSocket* socket, const QByteArray& request)
{
    // Parse the HTTP request line
    int lineEnd = request.indexOf("\r\n");
    if (lineEnd < 0) {
        lineEnd = request.indexOf("\n");
    }
    if (lineEnd < 0) {
        sendError(socket, 400, "Bad Request");
        return;
    }

    QByteArray requestLine = request.left(lineEnd);
    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendError(socket, 400, "Bad Request");
        return;
    }

    QByteArray method = parts[0];
    QByteArray path = parts[1];

    if (method == "GET" && path == "/api/v1/screenshot") {
        handleScreenshot(socket);
    } else if (method == "GET" && path.startsWith("/api/v1/probe?")) {
        handleProbe(socket, path);
    } else if (method == "POST" && path == "/api/v1/window/hide") {
        handleWindowHide(socket);
    } else if (method == "POST" && path == "/api/v1/window/show") {
        handleWindowShow(socket);
    } else {
        sendError(socket, 404, "Not Found");
    }
}

void LocalServer::handleScreenshot(QTcpSocket* socket)
{
#ifdef Q_OS_MACOS
    // Use macOS screencapture command which uses the private SkyLight
    // framework to capture across all Spaces and SDL Metal layers.
    // ScreenCaptureKit/CGDisplayCreateImage cannot see SDL Metal content
    // from the stream process, but screencapture can.
    QString path = "/tmp/hydra-screenshot.jpg";
    QProcess process;
    process.start("screencapture", QStringList() << "-x" << "-t" << "jpg" << path);
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        sendError(socket, 500, "screencapture failed");
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        sendError(socket, 500, "Failed to read screenshot file");
        return;
    }

    QByteArray jpegData = file.readAll();
    file.close();
    QFile::remove(path);

    sendResponse(socket, 200, "image/jpeg", jpegData);
#else
    sendError(socket, 501, "Screenshots only supported on macOS");
#endif
}

void LocalServer::handleProbe(QTcpSocket* socket, const QByteArray& path)
{
    // Parse query parameters: /api/v1/probe?host=IP&port=PORT
    QByteArray query = path.mid(path.indexOf('?') + 1);
    QList<QByteArray> params = query.split('&');

    QString host;
    quint16 port = 0;

    for (const QByteArray& param : params) {
        int eq = param.indexOf('=');
        if (eq < 0) continue;
        QByteArray key = param.left(eq);
        QByteArray value = param.mid(eq + 1);
        if (key == "host") host = QString::fromUtf8(value);
        else if (key == "port") port = value.toUShort();
    }

    if (host.isEmpty() || port == 0) {
        sendError(socket, 400, "host and port query parameters required");
        return;
    }

    // TCP connectivity probe with 1 second timeout.
    // This runs under the Qt app's TCC Local Network permission.
    QTcpSocket probe;
    probe.connectToHost(host, port);
    bool connected = probe.waitForConnected(1000);
    probe.close();

    QByteArray body = connected ? "{\"reachable\":true}" : "{\"reachable\":false}";
    sendResponse(socket, 200, "application/json", body);
}

void LocalServer::handleWindowHide(QTcpSocket* socket)
{
    QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        window->hide();
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LocalServer: kiosk window hidden");
    sendResponse(socket, 200, "application/json", "{\"status\":\"hidden\"}");
}

void LocalServer::handleWindowShow(QTcpSocket* socket)
{
    QWindowList windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        window->show();
        window->raise();
        window->requestActivate();
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LocalServer: kiosk window shown");
    sendResponse(socket, 200, "application/json", "{\"status\":\"shown\"}");
}

void LocalServer::sendResponse(QTcpSocket* socket, int statusCode,
                                const QByteArray& contentType, const QByteArray& body)
{
    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(" OK\r\n");
    response.append("Content-Type: ");
    response.append(contentType);
    response.append("\r\n");
    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void LocalServer::sendError(QTcpSocket* socket, int statusCode, const QString& message)
{
    QByteArray body = message.toUtf8();
    QByteArray response;
    response.append("HTTP/1.1 ");
    response.append(QByteArray::number(statusCode));
    response.append(" ");
    response.append(body);
    response.append("\r\n");
    response.append("Content-Type: text/plain\r\n");
    response.append("Content-Length: ");
    response.append(QByteArray::number(body.size()));
    response.append("\r\n");
    response.append("Connection: close\r\n");
    response.append("\r\n");
    response.append(body);

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}
