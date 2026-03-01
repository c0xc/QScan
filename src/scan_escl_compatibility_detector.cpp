/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (philip@c0xc.net)
** This file is part of QScan.
**
** QScan is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** QScan is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with QScan. If not, see <http://www.gnu.org/licenses/>.
**
****************************************************************************/

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTcpSocket>
#include <QTimer>

#include "scan/escl_compatibility_detector.hpp"
#include "core/classlogger.hpp"

namespace escl
{

static const int SOAP_HT_PORT = 8289;

bool
isSoapHtPortOpen(const QString &host, int timeout_ms)
{
    QTcpSocket socket;
    QEventLoop loop;
    QTimer timer;

    timer.setSingleShot(true);
    bool timeout_fired = false;

    QObject::connect(&timer, &QTimer::timeout, [&timeout_fired]() { timeout_fired = true; });
    QObject::connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    QObject::connect(&socket, &QTcpSocket::errorOccurred, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    socket.connectToHost(host, SOAP_HT_PORT);
    timer.start(timeout_ms);
    loop.exec();
    timer.stop();

    const bool connected = (socket.state() == QAbstractSocket::ConnectedState);
    socket.abort();

    Debug(QS("SOAP-HT port probe %s:%d -> %s",
             CSTR(host), SOAP_HT_PORT,
             connected ? "open" : (timeout_fired ? "timeout" : "closed")));

    return connected;
}

bool
fetchScannerCapabilities(const QString &host,
                        int timeout_ms,
                        QString *server_header_out,
                        QByteArray *xml_out)
{
    if (server_header_out)
        server_header_out->clear();
    if (xml_out)
        xml_out->clear();

    const QStringList urls = {
        QStringLiteral("https://%1/eSCL/ScannerCapabilities").arg(host),
        QStringLiteral("http://%1/eSCL/ScannerCapabilities").arg(host),
    };

    for (const QString &url_str : urls)
    {
        const QUrl url(url_str);

        QNetworkAccessManager net;
        QNetworkRequest req(url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QScan"));
        req.setRawHeader("Accept", "application/xml");

#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        bool timeout_fired = false;

        QNetworkReply *reply = net.get(req);

        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);

        QObject::connect(reply, &QNetworkReply::sslErrors, [reply, url](const QList<QSslError> &errors)
        {
            Debug(QS("eSCL detector ignoring SSL errors for %s", CSTR(url.toString())));
            reply->ignoreSslErrors();
        });

        QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&timeout_fired]() { timeout_fired = true; });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(timeout_ms);
        loop.exec();
        timer.stop();

        const bool ok = (reply->error() == QNetworkReply::NoError);
        if (ok)
        {
            const QByteArray body = reply->readAll();
            if (!body.isEmpty())
            {
                if (server_header_out)
                    *server_header_out = reply->header(QNetworkRequest::ServerHeader).toString();
                if (xml_out)
                    *xml_out = body;

                Debug(QS("eSCL detector ScannerCapabilities OK from %s", CSTR(url_str)));
                delete reply;
                return true;
            }
        }

        if (timeout_fired)
            Debug(QS("eSCL detector ScannerCapabilities timeout for %s", CSTR(url_str)));

        delete reply;
    }

    Debug(QS("eSCL detector ScannerCapabilities failed for %s", CSTR(host)));
    return false;
}

CompatibilityResult
detectCompatibility(const QString &host,
                   int timeout_ms,
                   DetectionResult *detail_out)
{
    DetectionResult result;

    result.port8289Open = isSoapHtPortOpen(host, timeout_ms);
    if (result.port8289Open)
    {
        Debug(QS("eSCL detector %s: SOAP-HT (port %d open)", CSTR(host), SOAP_HT_PORT));
        if (detail_out)
            *detail_out = result;
        return CompatibilityResult::SoapHt;
    }

    QString server_header;
    QByteArray xml;
    const bool caps_ok = fetchScannerCapabilities(host, timeout_ms, &server_header, &xml);
    result.serverHeader = server_header;

    if (!caps_ok)
    {
        Debug(QS("eSCL detector %s: ScannerCapabilities failed", CSTR(host)));
        if (detail_out)
            *detail_out = result;
        return CompatibilityResult::NotEscl;
    }

    if (server_header.contains("gSOAP", Qt::CaseInsensitive))
    {
        Debug(QS("eSCL detector %s: SOAP-HT (gSOAP detected)", CSTR(host)));
        if (detail_out)
            *detail_out = result;
        return CompatibilityResult::SoapHt;
    }

    Debug(QS("eSCL detector %s: compatible (server: %s)", CSTR(host), CSTR(server_header)));
    if (detail_out)
        *detail_out = result;
    return CompatibilityResult::Compatible;
}

bool
isCompatible(CompatibilityResult result)
{
    return result == CompatibilityResult::Compatible;
}

} //namespace escl
