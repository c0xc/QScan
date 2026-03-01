/****************************************************************************
**
** Copyright (C) 2025 Philip Seeger (p@c0xc.net)
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

#include "scan/escl_probe.hpp"
#include "core/classlogger.hpp"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QSslError>

namespace
{

static QUrl
esclUrl(const QUrl &base, const QString &suffix)
{
    QUrl out(base);

    QString path = out.path();
    if (path.isEmpty())
        path = QStringLiteral("/");

    if (!path.endsWith('/'))
        path += '/';

    out.setPath(path + suffix);
    return out;
}

} //namespace

bool
esclProbeScannerCapabilities(const QUrl &escl_base_url,
                            int timeout_ms,
                            QString *error_out,
                            QByteArray *caps_xml_out,
                            QUrl *effective_base_url_out)
{
    if (error_out)
        error_out->clear();
    if (caps_xml_out)
        caps_xml_out->clear();
    if (effective_base_url_out)
        effective_base_url_out->clear();

    if (!escl_base_url.isValid() || escl_base_url.host().isEmpty())
    {
        if (error_out)
            *error_out = QStringLiteral("Invalid URL");
        return false;
    }

    const QUrl url = esclUrl(escl_base_url, QStringLiteral("ScannerCapabilities"));

    Debug(QS("eSCL probe GET %s", CSTR(url.toString())));

    QNetworkAccessManager net;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QScan"));

#if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
#endif

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool timeout_fired = false;

    QStringList ssl_errors;

    QNetworkReply *reply = net.get(req);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    QObject::connect(reply, &QNetworkReply::sslErrors, [reply, url, &ssl_errors](const QList<QSslError> &errors)
    {
        for (const QSslError &e : errors)
            ssl_errors << e.errorString();

        Debug(QS("eSCL probe ignoring SSL errors for %s", CSTR(url.toString())));
        reply->ignoreSslErrors();
    });

    QObject::connect(&timer, &QTimer::timeout, [&timeout_fired, reply]()
    {
        timeout_fired = true;
        reply->abort();
    });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (timeout_ms > 0)
        timer.start(timeout_ms);

    loop.exec();
    timer.stop();

    const int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    Debug(QS("eSCL probe done qt_error=%d http_status=%d ssl_errors=%d", (int)reply->error(), http_status, (int)ssl_errors.size()));
    if (!ssl_errors.isEmpty())
        Debug(QS("eSCL probe ssl: %s", CSTR(ssl_errors.join(QStringLiteral(" | ")))));

    const bool ok = (reply->error() == QNetworkReply::NoError);
    if (ok)
    {
        const QByteArray body = reply->readAll();
        Debug(QS("eSCL probe ok bytes=%lld", (long long)body.size()));
        if (body.isEmpty())
        {
            if (error_out)
                *error_out = QStringLiteral("Empty response");
            delete reply;
            return false;
        }

        if (caps_xml_out)
            *caps_xml_out = body;

        if (effective_base_url_out)
        {
            QUrl effective_caps_url = reply->url();
            if (effective_caps_url.isValid())
            {
                QString path = effective_caps_url.path();
                if (path.endsWith(QStringLiteral("/ScannerCapabilities")))
                {
                    path.chop(QStringLiteral("/ScannerCapabilities").size());
                    effective_caps_url.setPath(path);
                }

                *effective_base_url_out = effective_caps_url;
            }
        }

        delete reply;
        return true;
    }

    if (error_out)
    {
        if (timeout_fired)
        {
            *error_out = QStringLiteral("Timeout");
        }
        else if (http_status > 0)
        {
            *error_out = QStringLiteral("HTTP %1: %2").arg(http_status).arg(reply->errorString());
        }
        else
        {
            *error_out = reply->errorString();
        }
    }

    delete reply;
    return false;
}
