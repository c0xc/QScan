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

#include <memory>
#include <atomic>

#include <QBuffer>
#include <QByteArray>
#include <QEventLoop>
#include <QMutex>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QNetworkAccessManager>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSslError>

#include "scan/scanner_backend.hpp"
#include "core/classlogger.hpp"
#include "core/profilesettings.hpp"

namespace
{

static const char*
ESCL_PREFIX()
{
    return "escl:";
}

static QUrl
parseEsclDeviceName(const QString &device_name)
{
    if (device_name.startsWith(ESCL_PREFIX()))
        return QUrl(device_name.mid((int)strlen(ESCL_PREFIX())));

    //Accept raw URLs as well (for forward compatibility)
    return QUrl(device_name);
}

static QUrl
ensureEsclBaseUrl(const QUrl &u)
{
    if (!u.isValid())
        return QUrl();

    QUrl out = u;
    QString path = out.path();
    if (path.isEmpty())
        path = "/";

    const int idx = path.indexOf("/eSCL");
    if (idx >= 0)
        path = path.left(idx + 5);
    else if (path == "/")
        path = "/eSCL";

    out.setPath(path);
    return out;
}

static bool
esclSameEndpointIgnoreScheme(const QUrl &a, const QUrl &b)
{
    if (!a.isValid() || !b.isValid())
        return false;

    const int port_a = a.port(-1);
    const int port_b = b.port(-1);
    if (port_a != port_b)
        return false;

    QString path_a = a.path();
    QString path_b = b.path();
    while (path_a.endsWith('/') && path_a != "/")
        path_a.chop(1);
    while (path_b.endsWith('/') && path_b != "/")
        path_b.chop(1);

    return a.host().compare(b.host(), Qt::CaseInsensitive) == 0 && path_a == path_b;
}

static bool
esclIgnoreCertErrorsForBaseUrl(const QUrl &base_url)
{
    ProfileSettings *settings = ProfileSettings::useDefaultProfile();
    if (!settings)
        return false;

    const QVariantList list = settings->variant("network_scanners").toList();
    for (const QVariant &v : list)
    {
        const QVariantMap m = v.toMap();
        const QUrl u(m.value("url").toString().trimmed());
        if (!u.isValid())
            continue;

        if (esclSameEndpointIgnoreScheme(u, base_url))
            return m.value("ignore_cert_error", false).toBool();
    }

    return false;
}

static QUrl
esclUrl(const QUrl &base, const QString &suffix)
{
    //base expected to end with /eSCL
    QUrl out = base;
    QString p = out.path();
    if (!p.endsWith('/'))
        p += '/';
    out.setPath(p + suffix);
    return out;
}

static QString
mapColorModeToEscl(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m == "gray" || m == "grey" || m == "grayscale" || m == "greyscale")
        return QStringLiteral("Grayscale8");
    if (m == "bw" || m == "mono" || m == "monochrome" || m == "binary")
        return QStringLiteral("BlackAndWhite1");
    return QStringLiteral("RGB24");
}

static QString
mapEsclColorModeToUi(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m.contains("gray") || m.contains("grey"))
        return QStringLiteral("Gray");
    if (m.contains("blackandwhite") || m.contains("mono") || m.contains("binary"))
        return QStringLiteral("BW");
    return QStringLiteral("Color");
}

class EsclScannerBackend final : public ScannerBackend
{
public:

    bool
    initialize(const QString &device_name) override
    {
        //NOTE: eSCL field testing (2026-02)
        //-GOOD HP LaserJet Pro MFP 3102fdw: eSCL scanning tested working with QScan
        //-BAD  HP Color LaserJet MFP M476dw (NPI3081A3): eSCL discovery works
        //(ScannerCapabilities + ScannerStatus, incl. ADF state), but ScanJobs creation returns HTTP 400
        //with an empty response body for many ScanSettings variants
        //See RESEARCH_NOTES/HP_M476DW_ESCL_SCANJOBS_400.md

        m_base_url = ensureEsclBaseUrl(parseEsclDeviceName(device_name));
        if (!m_base_url.isValid() || m_base_url.scheme().isEmpty() || m_base_url.host().isEmpty())
        {
            Debug(QS("eSCL initialize: invalid URL <%s>", CSTR(device_name)));
            return false;
        }

        m_ignore_cert_errors = esclIgnoreCertErrorsForBaseUrl(m_base_url);

        //Start with sane defaults, then refine from ScannerCapabilities
        m_capabilities = ScanCapabilities();
        m_capabilities.preview_mode = PreviewMode::SingleImage;

        //Fetch scanner capabilities (best-effort)
        QString error;
        const QByteArray xml = httpGetBytes(esclUrl(m_base_url, "ScannerCapabilities"), error, 8000);
        if (!xml.isEmpty())
        {
            //Parse capabilities XML into semantic flags/lists
            parseScannerCapabilities(xml);
        }
        else
        {
            Debug(QS("eSCL: ScannerCapabilities fetch failed: %s", CSTR(error)));
            //Do not guess capabilities here
            //Some devices may allow scanning even if capabilities are blocked (auth/firewall)
            //In that case we keep safe defaults (no ADF/duplex claimed) and allow flatbed scans
        }

        //Fetch scanner status (best-effort, GET-only)
        //some devices report ADF state here, currently read-only
        {
            QString status_error;
            const QByteArray status_xml = httpGetBytes(esclUrl(m_base_url, "ScannerStatus"), status_error, 3000);
            if (!status_xml.isEmpty())
                parseScannerStatus(status_xml);
            else if (!status_error.isEmpty())
                Debug(QS("eSCL: ScannerStatus fetch failed: %s", CSTR(status_error)));
        }

        m_is_open = true;
        return true;
    }

    void
    cancelScan() override
    {
        m_cancel_requested.store(true);

        //Send abort request to the currently active HTTP reply
        QMutexLocker lock(&m_reply_mutex);
        if (m_active_reply)
        {
            QNetworkReply *reply = m_active_reply;
            lock.unlock();

            if (reply->thread() == QThread::currentThread())
                reply->abort();
            else
                QMetaObject::invokeMethod(reply, "abort", Qt::QueuedConnection);
        }
    }

    bool
    isOpen() const override
    {
        return m_is_open;
    }

    ScanCapabilities
    capabilities() const override
    {
        return m_capabilities;
    }

    QSizeF
    currentDocumentSize() const override
    {
        return m_current_document_size;
    }

    bool
    documentSizeIsReported() const override
    {
        return false;
    }

    bool
    scan(const ScanParameters &params, const PageCallback &on_page, QString &error_out) override
    {
        m_cancel_requested.store(false);

        bool stop_requested = false;

        const int res_used = params.resolution > 0 ? params.resolution
            : (m_default_resolution > 0 ? m_default_resolution : 300);

        Debug(QS("eSCL scan: base=<%s> res=%d mode=%s adf=%d duplex=%d",
                 CSTR(m_base_url.toString()),
                 res_used,
                 CSTR(params.color_mode),
                 (int)params.use_adf,
                 (int)params.use_duplex));

        //1)Create a scan job
        const QUrl scan_jobs = esclUrl(m_base_url, "ScanJobs");
        const QByteArray scan_settings_xml = buildScanSettingsXml(params);

        QString error;
        QString job_url;
        Debug(QS("eSCL scan: creating ScanJob via <%s>", CSTR(scan_jobs.toString())));
        if (!httpCreateScanJob(scan_jobs, scan_settings_xml, job_url, error, 15000))
        {
            if (m_cancel_requested.load())
                error_out = QString();
            else
                error_out = error.isEmpty() ? QStringLiteral("eSCL: failed to create scan job") : error;
            return false;
        }

        Debug(QS("eSCL scan: ScanJob created: <%s>", CSTR(job_url)));

        //2)Download pages via NextDocument until done or canceled
        bool got_any = false;
        int page = 0;

        while (!m_cancel_requested.load())
        {
            QByteArray img_data;
            QString page_error;
            const QUrl next_doc = QUrl(job_url + QStringLiteral("/NextDocument"));
            Debug(QS("eSCL scan: GET NextDocument <%s>", CSTR(next_doc.toString())));
            img_data = httpGetBytes(next_doc, page_error, 60000);

            if (m_cancel_requested.load())
                break;

            //Check for empty backend response
            if (img_data.isEmpty())
            {
                //No more pages counts as success if at least one page was received
                //if none and also have an error, surface it
                if (!got_any && !page_error.isEmpty())
                {
                    Debug(QS("eSCL scan: NextDocument failed before first page: %s", CSTR(page_error)));
                    error_out = page_error;
                    httpDeleteJob(job_url);
                    return false;
                }
                break;
            }

            //Load received image data into image
            QImage img = QImage::fromData(img_data);
            if (img.isNull())
            {
                error_out = QStringLiteral("eSCL: received unsupported image data");
                httpDeleteJob(job_url);
                return false;
            }

            if (res_used > 0)
            {
                const qreal width_mm = (qreal)img.width() * 25.4 / (qreal)res_used;
                const qreal height_mm = (qreal)img.height() * 25.4 / (qreal)res_used;
                m_current_document_size = QSizeF(width_mm, height_mm);
            }

            got_any = true;
            qscan::ScanPageInfo page_info;
            page_info.backend_kind = QStringLiteral("eSCL");
            page_info.backend_details.insert(QStringLiteral("escl.page_number"), page);
            if (res_used > 0)
                page_info.backend_details.insert(QStringLiteral("escl.resolution_used"), res_used);
            if (!on_page(img, page, page_info))
            {
                stop_requested = true;
                break;
            }

            //Platen scans are single-page even if the device keeps returning pages
            if (!params.use_adf)
                break;

            page++;
        }

        //Attempt to delete the scan job on the device
        httpDeleteJob(job_url);

        if (m_cancel_requested.load())
        {
            error_out = QString();
            return false;
        }

        //Consumer asked to stop early (e.g. preview wants only the first page)
        if (stop_requested && got_any)
            return true;

        if (!got_any)
        {
            error_out = QStringLiteral("eSCL: no pages received");
            return false;
        }

        return true;
    }

private:

    QByteArray
    httpGetBytes(const QUrl &url, QString &error_out, int timeout_ms)
    {
        QNetworkAccessManager net;
        //Perform synchronous GET (worker-thread only)
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
        {
            QMutexLocker lock(&m_reply_mutex);
            //Expose active reply to cancelScan()
            m_active_reply = reply;
        }

        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);

        QObject::connect(reply, &QNetworkReply::sslErrors, [this, reply, url, &ssl_errors](const QList<QSslError> &errors)
        {
            for (const QSslError &e : errors)
                ssl_errors << e.errorString();

            if (!m_ignore_cert_errors)
            {
                Debug(QS("eSCL SSL errors (not ignored) for %s", CSTR(url.toString())));
                return;
            }

            Debug(QS("eSCL ignoring SSL errors for %s", CSTR(url.toString())));
            reply->ignoreSslErrors();
        });

        //Abort request on timeout
        QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&timeout_fired]() { timeout_fired = true; });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        if (timeout_ms > 0)
            timer.start(timeout_ms);

        loop.exec();
        timer.stop();

        {
            QMutexLocker lock(&m_reply_mutex);
            if (m_active_reply == reply)
                m_active_reply = nullptr;
        }

        QByteArray out;
        if (reply->error() == QNetworkReply::NoError)
        {
            //Return raw payload (image bytes or XML)
            out = reply->readAll();
        }
        else if (!m_cancel_requested.load())
        {
            //Surface network error to caller (unless we were canceled)
            if (timeout_fired)
                error_out = QStringLiteral("Timeout");
            else
                error_out = reply->errorString();

            const QByteArray body = reply->readAll();
            if (!body.isEmpty())
            {
                const int limit = 4096;
                Debug(QS("eSCL HTTP error body (%d bytes): %s", (int)body.size(), CSTR(QString::fromUtf8(body.left(limit)))));
            }

            const QVariant http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const QVariant http_reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
            if (http_code.isValid())
            {
                error_out = QStringLiteral("HTTP %1 %2 (%3)")
                    .arg(http_code.toInt())
                    .arg(http_reason.toString())
                    .arg(error_out);
            }
        }

        //Delete reply synchronously (do not rely on a running event loop)
        reply->setParent(nullptr);
        delete reply;
        return out;
    }

    bool
    httpCreateScanJob(const QUrl &scan_jobs_url, const QByteArray &body, QString &job_url_out, QString &error_out, int timeout_ms)
    {
        QNetworkAccessManager net;
        //POST ScanSettings XML to create a ScanJob
        QNetworkRequest req(scan_jobs_url);
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QScan"));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml"));

    #if QT_VERSION >= QT_VERSION_CHECK(5, 9, 0)
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    #endif

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        bool timeout_fired = false;

        QStringList ssl_errors;

        QNetworkReply *reply = net.post(req, body);
        {
            QMutexLocker lock(&m_reply_mutex);
            //Expose active reply to cancelScan()
            m_active_reply = reply;
        }

        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);

        QObject::connect(reply, &QNetworkReply::sslErrors, [this, reply, scan_jobs_url, &ssl_errors](const QList<QSslError> &errors)
        {
            for (const QSslError &e : errors)
                ssl_errors << e.errorString();

            if (!m_ignore_cert_errors)
                return;

            Debug(QS("eSCL ignoring SSL errors for %s", CSTR(scan_jobs_url.toString())));
            reply->ignoreSslErrors();
        });

        //Abort request on timeout
        QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
        QObject::connect(&timer, &QTimer::timeout, &loop, [&timeout_fired]() { timeout_fired = true; });
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        if (timeout_ms > 0)
            timer.start(timeout_ms);

        loop.exec();
        timer.stop();

        {
            QMutexLocker lock(&m_reply_mutex);
            if (m_active_reply == reply)
                m_active_reply = nullptr;
        }

        if (m_cancel_requested.load())
        {
            reply->setParent(nullptr);
            delete reply;
            return false;
        }

        auto logRequestXmlIfFailure = [&]()
        {
            if (reply->error() == QNetworkReply::NoError)
                return;

            if (body.isEmpty())
            {
                Debug(QStringLiteral("eSCL ScanJobs request XML: <empty>"));
                return;
            }

            const int limit = 16384;
            const QByteArray to_log = body.left(limit);
            Debug(QS("eSCL ScanJobs request XML (%d bytes, first %d) to %s: %s",
                (int)body.size(), (int)to_log.size(), CSTR(scan_jobs_url.toString()), to_log.constData()));
        };

        const QByteArray resp_body = reply->readAll();
        if (!resp_body.isEmpty())
        {
            const int limit = 4096;
            Debug(QS("eSCL ScanJobs response body (%d bytes): %s", (int)resp_body.size(), CSTR(QString::fromUtf8(resp_body.left(limit)))));
        }

        //Job URL is typically provided via Location header
        const QVariant loc = reply->header(QNetworkRequest::LocationHeader);
        if (reply->error() == QNetworkReply::NoError && loc.isValid())
        {
            const QUrl job_url = loc.toUrl();
            if (job_url.isValid())
            {
                //Some devices return a relative Location (e.g. /eSCL/ScanJobs/123)
                job_url_out = job_url.isRelative() ? m_base_url.resolved(job_url).toString() : job_url.toString();
                reply->setParent(nullptr);
                delete reply;
                return true;
            }
        }

        if (reply->error() == QNetworkReply::NoError)
        {
            const QVariant http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const QVariant http_reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
            if (http_code.isValid())
                error_out = QStringLiteral("eSCL: missing job Location header (HTTP %1 %2)")
                    .arg(http_code.toInt())
                    .arg(http_reason.toString());
            else
                error_out = QStringLiteral("eSCL: missing job Location header");
        }
        else
        {
            logRequestXmlIfFailure();
            QString net_err;
            if (timeout_fired)
                net_err = QStringLiteral("Timeout");
            else
                net_err = reply->errorString();
            const QVariant http_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            const QVariant http_reason = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);
            if (http_code.isValid())
                error_out = QStringLiteral("HTTP %1 %2 (%3)")
                    .arg(http_code.toInt())
                    .arg(http_reason.toString())
                    .arg(net_err);
            else
                error_out = net_err;
        }
        reply->setParent(nullptr);
        delete reply;
        return false;
    }

    void
    httpDeleteJob(const QString &job_url)
    {
        if (job_url.isEmpty())
            return;

        QNetworkAccessManager net;
        //Best-effort DELETE with a short timeout
        QNetworkRequest req{QUrl(job_url)};
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("QScan"));

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        QNetworkReply *reply = net.sendCustomRequest(req, QByteArrayLiteral("DELETE"));
        {
            QMutexLocker lock(&m_reply_mutex);
            //Expose active reply to cancelScan()
            m_active_reply = reply;
        }

        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);

        QObject::connect(reply, &QNetworkReply::sslErrors, [this, reply](const QList<QSslError> &errors)
        {
            if (!m_ignore_cert_errors)
                return;
            for (const QSslError &e : errors)
                Debug(QS("eSCL SSL error ignored: %s", CSTR(e.errorString())));
            reply->ignoreSslErrors();
        });
        timer.start(2000);
        loop.exec();

        {
            QMutexLocker lock(&m_reply_mutex);
            if (m_active_reply == reply)
                m_active_reply = nullptr;
        }

        reply->setParent(nullptr);
        delete reply;
    }

    QByteArray
    buildScanSettingsXml(const ScanParameters &params) const
    {
        //Keep minimal, map semantic params to common eSCL fields
        QByteArray out;
        QXmlStreamWriter w(&out);
        w.setAutoFormatting(false);
        w.writeStartDocument();

        w.writeStartElement(QStringLiteral("scan:ScanSettings"));
        w.writeAttribute(QStringLiteral("xmlns:scan"), QStringLiteral("http://schemas.hp.com/imaging/escl/2011/05/03"));
        w.writeAttribute(QStringLiteral("xmlns:pwg"), QStringLiteral("http://www.pwg.org/schemas/2010/12/sm"));

        //Select scan settings from UI parameters (fallback to capability-derived defaults)
        const int res = params.resolution > 0 ? params.resolution : (m_default_resolution > 0 ? m_default_resolution : 300);
        const QString color = params.color_mode.isEmpty() ? (m_default_color_mode.isEmpty() ? QStringLiteral("Color") : m_default_color_mode)
                                  : params.color_mode;

        w.writeTextElement(QStringLiteral("pwg:Version"), QStringLiteral("2.1"));

        QString doc_format;
        if (!m_supported_document_formats.isEmpty())
        {
            const QString prefer = QStringLiteral("image/jpeg");
            doc_format = m_supported_document_formats.contains(prefer) ? prefer : m_supported_document_formats.first();
        }
        if (!doc_format.isEmpty())
        {
            if (m_supports_document_format_ext)
                w.writeTextElement(QStringLiteral("scan:DocumentFormatExt"), doc_format);
            else if (m_supports_document_format)
                w.writeTextElement(QStringLiteral("pwg:DocumentFormat"), doc_format);
            else
                w.writeTextElement(QStringLiteral("scan:DocumentFormatExt"), doc_format);
        }

        w.writeTextElement(QStringLiteral("scan:ColorMode"), mapColorModeToEscl(color));
        w.writeTextElement(QStringLiteral("scan:XResolution"), QString::number(res));
        w.writeTextElement(QStringLiteral("scan:YResolution"), QString::number(res));

        //Input source mapping (ADF/duplex semantics)
        QString input = QStringLiteral("Platen");
        if (params.use_adf)
            input = QStringLiteral("Adf");
        if (params.use_adf && params.use_duplex && m_capabilities.supports_duplex)
            input = QStringLiteral("AdfDuplex");
        w.writeTextElement(QStringLiteral("scan:InputSource"), input);

        w.writeEndElement();
        w.writeEndDocument();

        return out;
    }

    void
    parseScannerCapabilities(const QByteArray &xml)
    {
        QXmlStreamReader r(xml);

        QList<int> resolutions;
        QStringList ui_color_modes;
        QStringList doc_formats;
        bool has_doc_format_ext = false;
        bool has_doc_format = false;
        bool has_platen = false;
        bool has_adf = false;
        bool has_duplex = false;

        while (!r.atEnd())
        {
            r.readNext();
            if (!r.isStartElement())
                continue;

            const QString name = r.name().toString();

            if (name == QStringLiteral("Platen"))
                has_platen = true;
            else if (name == QStringLiteral("Adf"))
                has_adf = true;
            else if (name.toLower().contains("duplex"))
                has_duplex = true;
            else if (name == QStringLiteral("XResolution") || name == QStringLiteral("YResolution"))
            {
                const int v = r.readElementText().toInt();
                if (v > 0)
                    resolutions << v;
            }
            else if (name == QStringLiteral("ColorMode"))
            {
                const QString cm = mapEsclColorModeToUi(r.readElementText());
                if (!ui_color_modes.contains(cm))
                    ui_color_modes << cm;
            }
            else if (name == QStringLiteral("DocumentFormatExt") || name == QStringLiteral("DocumentFormat"))
            {
                if (name == QStringLiteral("DocumentFormatExt"))
                    has_doc_format_ext = true;
                else
                    has_doc_format = true;

                const QString fmt = r.readElementText().trimmed();
                if (!fmt.isEmpty() && !doc_formats.contains(fmt))
                    doc_formats << fmt;
            }
        }

        //Deduplicate + sort resolutions
        std::sort(resolutions.begin(), resolutions.end());
        resolutions.erase(std::unique(resolutions.begin(), resolutions.end()), resolutions.end());
        if (!resolutions.isEmpty())
            m_capabilities.supported_resolutions = resolutions;

        if (!ui_color_modes.isEmpty())
            m_capabilities.supported_color_modes = ui_color_modes;

        m_capabilities.supports_auto_feed = has_adf;
        m_capabilities.supports_multi_page = has_adf;
        m_capabilities.supports_duplex = has_duplex;
        m_capabilities.supports_auto_page_size = true;

        m_supported_document_formats = doc_formats;
        m_supports_document_format_ext = has_doc_format_ext;
        m_supports_document_format = has_doc_format;

        m_capabilities.supported_input_sources.clear();
        if (has_platen)
            m_capabilities.supported_input_sources << QStringLiteral("Flatbed");
        if (has_adf)
            m_capabilities.supported_input_sources << QStringLiteral("ADF");

        //Pick defaults used by scan settings builder
        m_default_resolution = m_capabilities.supported_resolutions.isEmpty() ? 300 : m_capabilities.supported_resolutions.first();
        m_default_color_mode = m_capabilities.supported_color_modes.isEmpty() ? QStringLiteral("Color") : m_capabilities.supported_color_modes.first();
    }

    void
    parseScannerStatus(const QByteArray &xml)
    {
        QXmlStreamReader r(xml);

        QString state;
        QString adf_state;

        while (!r.atEnd())
        {
            r.readNext();
            if (!r.isStartElement())
                continue;

            const QString name = r.name().toString();
            if (name == QStringLiteral("State"))
                state = r.readElementText().trimmed();
            else if (name == QStringLiteral("AdfState"))
                adf_state = r.readElementText().trimmed();
        }

        if (!state.isEmpty())
            m_last_scanner_state = state;
        if (!adf_state.isEmpty())
            m_last_adf_state = adf_state;

        if (!m_last_scanner_state.isEmpty() || !m_last_adf_state.isEmpty())
            Debug(QS("eSCL ScannerStatus: state=%s adf=%s", CSTR(m_last_scanner_state), CSTR(m_last_adf_state)));
    }

    QUrl m_base_url;
    ScanCapabilities m_capabilities;
    bool m_is_open = false;

    bool m_ignore_cert_errors = false;

    QStringList m_supported_document_formats;

    bool m_supports_document_format_ext = false;
    bool m_supports_document_format = false;

    //Defaults for minimal scan settings
    int m_default_resolution = 300;
    QString m_default_color_mode = QStringLiteral("Color");

    QSizeF m_current_document_size;

    //Last observed status values (unused for now; stored for future UX/automation)
    QString m_last_scanner_state;
    QString m_last_adf_state;

    //Networking + cancellation
    std::atomic<bool> m_cancel_requested{false};

    mutable QMutex m_reply_mutex;
    QNetworkReply *m_active_reply = nullptr;

};

} //namespace

extern std::unique_ptr<ScannerBackend>
createScannerBackend_ESCL()
{
    return std::unique_ptr<ScannerBackend>(new EsclScannerBackend());
}
