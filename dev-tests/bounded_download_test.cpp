#include "include/global/BoundedDownload.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QTcpServer>
#include <QTcpSocket>
#include <cassert>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using namespace Configs_network;
    assert(isHttpUrl(QUrl("https://example.test/path")));
    assert(!isHttpUrl(QUrl("file:///private/config.json")));
    assert(!isHttpUrl(QUrl("https:///missing-host")));

    const auto request = [](const QByteArray &response, qsizetype limit, bool drip = false) {
        QTcpServer server;
        assert(server.listen(QHostAddress::LocalHost, 0));
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [=] {
                socket->readAll();
                socket->write(response);
                if (drip) {
                    auto timer = new QTimer(socket);
                    QObject::connect(timer, &QTimer::timeout, socket, [=] { socket->write("x"); });
                    timer->start(10);
                }
            });
        });
        QNetworkAccessManager manager;
        manager.setProxy(QNetworkProxy::NoProxy);
        auto reply = manager.get(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()))));
        QElapsedTimer elapsed;
        elapsed.start();
        auto result = readBoundedReply(reply, limit, 200);
        assert(elapsed.elapsed() < 3000);
        return result;
    };
    const auto ok = request("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", 5);
    assert(ok.error.isEmpty() && ok.data == "hello");
    const auto large = request("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n123456", 5);
    assert(!large.error.isEmpty() && large.data.isEmpty());
    const auto error = request("HTTP/1.1 503 Unavailable\r\nContent-Length: 4\r\n\r\noops", 100);
    assert(!error.error.isEmpty() && error.data.isEmpty());
    const auto stalled = request("HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n", 1000);
    assert(stalled.error.contains("deadline") && stalled.data.isEmpty());
    const auto drip = request("HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n", 1000, true);
    assert(drip.error.contains("deadline") && drip.data.isEmpty());
}
