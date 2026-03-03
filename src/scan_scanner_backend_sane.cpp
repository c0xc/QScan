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

#include <algorithm>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <strings.h>

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>

#include "core/classlogger.hpp"
#include "scan/scan_device_info.hpp"
#include "scan/scanner_backend.hpp"

#ifndef SANE_NAME_SCAN_RESOLUTION
#define SANE_NAME_SCAN_RESOLUTION "resolution"
#endif

#ifndef SANE_NAME_SCAN_MODE
#define SANE_NAME_SCAN_MODE "mode"
#endif

#ifndef SANE_NAME_SCAN_SOURCE
#define SANE_NAME_SCAN_SOURCE "source"
#endif

static bool g_sane_initialized = false;
static int g_sane_ref_count = 0;

static bool g_sane_ld_library_path_sanitized = false;
static QByteArray g_sane_saved_ld_library_path;

static bool g_sane_debug_env_applied = false;
static QByteArray g_sane_saved_sane_debug_dll;

static QByteArray
joinListForLog(const QList<QByteArray> &parts)
{
    QByteArray out;
    for (int i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
            out += " | ";
        out += parts[i];
    }
    return out;
}

static bool
sanitizeLdLibraryPathForSane(const QByteArray &ld_library_path,
                            QByteArray &sanitized_out,
                            QList<QByteArray> &removed_out,
                            bool &appdir_available_out)
{
    sanitized_out = ld_library_path;
    removed_out.clear();

    if (ld_library_path.isEmpty())
    {
        //Nothing to do
        appdir_available_out = true;
        return false;
    }

    //When running from AppImage, prefer host libs/backends
    //keep user/host LD_LIBRARY_PATH entries that backends may rely on
    QByteArray appdir = qgetenv("APPDIR");
    if (appdir.isEmpty())
        appdir = qgetenv("QSCAN_APPDIR");
    if (appdir.isEmpty())
    {
        appdir_available_out = false;
        return false;
    }
    appdir_available_out = true;

    const QByteArray prefix1 = appdir + "/usr/lib";
    const QByteArray prefix2 = appdir + "/usr/lib64";

    const QList<QByteArray> parts = ld_library_path.split(':');
    QList<QByteArray> kept;
    kept.reserve(parts.size());

    for (const QByteArray &p : parts)
    {
        if (p.isEmpty())
            continue;

        if (p == prefix1 || p == prefix2 || p.startsWith(prefix1 + "/") || p.startsWith(prefix2 + "/"))
        {
            removed_out.append(p);
            continue;
        }

        kept.append(p);
    }

    sanitized_out = kept.join(":");
    return sanitized_out != ld_library_path;
}

static bool
shouldSanitizeLdLibraryPathForSane()
{
    const QByteArray enabled = qgetenv("QSCAN_SANE_SANITIZE_LD_LIBRARY_PATH");
    return enabled == "1" || enabled == "true" || enabled == "TRUE";
}

static void
maybeApplySaneDebugEnv()
{
    if (g_sane_debug_env_applied)
        return;

    const QByteArray qscan_debug_dll = qgetenv("QSCAN_SANE_DEBUG_DLL");
    if (qscan_debug_dll.isEmpty())
        return;

    g_sane_saved_sane_debug_dll = qgetenv("SANE_DEBUG_DLL");
    qputenv("SANE_DEBUG_DLL", qscan_debug_dll);
    g_sane_debug_env_applied = true;

    Debug(QS("SANE: forwarding debug: SANE_DEBUG_DLL=<%s> (from QSCAN_SANE_DEBUG_DLL)", qscan_debug_dll.constData()));
}

static void
maybeRestoreSaneDebugEnv()
{
    if (!g_sane_debug_env_applied)
        return;

    if (g_sane_ref_count != 0)
        return;

    if (g_sane_saved_sane_debug_dll.isEmpty())
        qunsetenv("SANE_DEBUG_DLL");
    else
        qputenv("SANE_DEBUG_DLL", g_sane_saved_sane_debug_dll);

    g_sane_saved_sane_debug_dll.clear();
    g_sane_debug_env_applied = false;
    Debug(QS("SANE: restored SANE_DEBUG_DLL"));
}

static void
logLoadedSaneLibraryForDebug()
{
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void *)&sane_init, &info) == 0)
    {
        Debug(QS("SANE: dladdr(sane_init) failed; cannot determine libsane path"));
        return;
    }

    const char *path = info.dli_fname ? info.dli_fname : "";
    Debug(QS("SANE: libsane loaded from <%s>", path));
}

static void
enterSaneEnvIfNeeded()
{
    if (!shouldSanitizeLdLibraryPathForSane())
        return;

    if (g_sane_ld_library_path_sanitized)
        return;

    maybeApplySaneDebugEnv();

    g_sane_saved_ld_library_path = qgetenv("LD_LIBRARY_PATH");
    Debug(QS("SANE: LD_LIBRARY_PATH before=<%s>", g_sane_saved_ld_library_path.constData()));

    QByteArray sanitized;
    QList<QByteArray> removed;
    bool appdir_available = false;
    const bool changed = sanitizeLdLibraryPathForSane(g_sane_saved_ld_library_path, sanitized, removed, appdir_available);

    //If APPDIR unavailable, keep LD_LIBRARY_PATH unchanged
    //avoids breaking host backends that rely on it
    if (!appdir_available)
    {
        Debug(QS("SANE: LD_LIBRARY_PATH sanitization enabled but APPDIR not available; leaving LD_LIBRARY_PATH unchanged"));
        Debug(QS("SANE: LD_LIBRARY_PATH after=<%s>", g_sane_saved_ld_library_path.constData()));
    }
    else if (!changed)
    {
        Debug(QS("SANE: LD_LIBRARY_PATH sanitization: no AppImage paths found; unchanged"));
        Debug(QS("SANE: LD_LIBRARY_PATH after=<%s>", g_sane_saved_ld_library_path.constData()));
    }
    else
    {
        Debug(QS("SANE: LD_LIBRARY_PATH removed %d entr%s: <%s>",
                 (int)removed.size(), removed.size() == 1 ? "y" : "ies", joinListForLog(removed).constData()));

        if (sanitized.isEmpty())
        {
            qunsetenv("LD_LIBRARY_PATH");
            Debug(QS("SANE: LD_LIBRARY_PATH after=<UNSET>"));
            Debug(QS("SANE: sanitized LD_LIBRARY_PATH (removed AppImage paths; now unset)"));
        }
        else
        {
            qputenv("LD_LIBRARY_PATH", sanitized);
            Debug(QS("SANE: LD_LIBRARY_PATH after=<%s>", sanitized.constData()));
            Debug(QS("SANE: sanitized LD_LIBRARY_PATH (removed AppImage paths)"));
        }
    }
    g_sane_ld_library_path_sanitized = true;
}

static void
leaveSaneEnvIfNeeded()
{
    if (!shouldSanitizeLdLibraryPathForSane())
        return;

    if (!g_sane_ld_library_path_sanitized)
        return;

    if (g_sane_ref_count != 0)
        return;

    if (g_sane_saved_ld_library_path.isEmpty())
        qunsetenv("LD_LIBRARY_PATH");
    else
        qputenv("LD_LIBRARY_PATH", g_sane_saved_ld_library_path);
    g_sane_saved_ld_library_path.clear();
    g_sane_ld_library_path_sanitized = false;
    Debug(QS("SANE: restored LD_LIBRARY_PATH"));

    maybeRestoreSaneDebugEnv();
}

static bool
ensureSaneInitialized()
{
    if (g_sane_initialized)
        return true;

    //Apply debug env just before sane_init() too
    //covers code paths that don't pass through enterSaneEnvIfNeeded()
    maybeApplySaneDebugEnv();

    logLoadedSaneLibraryForDebug();

    SANE_Int version = 0;
    SANE_Status status = sane_init(&version, 0);
    if (status != SANE_STATUS_GOOD)
    {
        Debug(QS("sane_init() FAILED: %d (%s)", status, sane_strstatus(status)));
        return false;
    }

    Debug(QS("SANE initialized, version: %d.%d.%d",
             SANE_VERSION_MAJOR(version), SANE_VERSION_MINOR(version), SANE_VERSION_BUILD(version)));
    g_sane_initialized = true;
    return true;
}

static void
logSaneHostSetupForDebug()
{
    const QByteArray sane_config_dir_env = qgetenv("SANE_CONFIG_DIR");
    const QString sane_config_dir = sane_config_dir_env.isEmpty() ? QStringLiteral("/etc/sane.d") : QString::fromLocal8Bit(sane_config_dir_env);

    Debug(QS("SANE host setup: SANE_CONFIG_DIR=%s", sane_config_dir_env.isEmpty() ? "<UNSET>" : sane_config_dir_env.constData()));

    const QString dll_conf = QDir(sane_config_dir).filePath(QStringLiteral("dll.conf"));
    Debug(QS("SANE host setup: dll.conf exists=%d path=<%s>", QFileInfo::exists(dll_conf) ? 1 : 0, CSTR(dll_conf)));

    const QStringList backend_dirs =
    {
        QStringLiteral("/usr/lib/sane"),
        QStringLiteral("/usr/lib64/sane"),
        QStringLiteral("/usr/local/lib/sane"),
        QStringLiteral("/usr/local/lib64/sane"),
    };

    for (const QString &dir_path : backend_dirs)
    {
        const QDir d(dir_path);
        if (!d.exists())
        {
            Debug(QS("SANE host setup: backend dir missing: <%s>", CSTR(dir_path)));
            continue;
        }

        const QStringList entries = d.entryList(QStringList() << QStringLiteral("libsane-*.so") << QStringLiteral("libsane-*.so.*"), QDir::Files);
        Debug(QS("SANE host setup: backend dir ok: <%s> modules=%d", CSTR(dir_path), (int)entries.size()));
    }

    const QByteArray dlopen_test = qgetenv("QSCAN_SANE_TEST_DLOPEN_BACKENDS");
    if (dlopen_test.isEmpty())
        return;

    const QList<QByteArray> backends = dlopen_test.split(',');
    Debug(QS("SANE host setup: dlopen test requested for %d backend(s)", (int)backends.size()));

    for (QByteArray backend : backends)
    {
        backend = backend.trimmed();
        if (backend.isEmpty())
            continue;

        const QString backend_name = QString::fromLocal8Bit(backend);
        QString found;
        for (const QString &dir_path : backend_dirs)
        {
            const QDir d(dir_path);
            if (!d.exists())
                continue;

            const QStringList matches = d.entryList(QStringList() << QStringLiteral("libsane-") + backend_name + QStringLiteral(".so*")
                                                                  << QStringLiteral("libsane-") + backend_name + QStringLiteral(".so.*"),
                                                    QDir::Files);
            if (!matches.isEmpty())
            {
                found = d.filePath(matches[0]);
                break;
            }
        }

        if (found.isEmpty())
        {
            Debug(QS("SANE host setup: dlopen test: backend <%s>: module not found in standard dirs", backend.constData()));
            continue;
        }

        Debug(QS("SANE host setup: dlopen test: backend <%s>: trying <%s>", backend.constData(), CSTR(found)));

        dlerror();
        void *h = dlopen(found.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
        if (!h)
        {
            const char *err = dlerror();
            Debug(QS("SANE host setup: dlopen test: backend <%s>: FAILED: %s", backend.constData(), err ? err : "<unknown>"));
            continue;
        }

        Debug(QS("SANE host setup: dlopen test: backend <%s>: OK", backend.constData()));
        dlclose(h);
    }
}

static void
maybeSaneExit()
{
    if (g_sane_ref_count == 0 && g_sane_initialized)
    {
        Debug(QS("Last SANE backend destroyed, calling sane_exit()"));
        sane_exit();
        g_sane_initialized = false;
    }

    //Even without LD_LIBRARY_PATH sanitization, debug env may be forwarded
    //restore env when SANE is no longer in use
    maybeRestoreSaneDebugEnv();
}

static bool
stringListContains(const SANE_String_Const *list, const char *needle)
{
    if (!list || !needle)
        return false;

    for (int i = 0; list[i] != 0; ++i)
    {
        if (strcmp(list[i], needle) == 0)
            return true;
    }
    return false;
}

static bool
convertLengthMmToSaneUnit(const SANE_Option_Descriptor *opt_desc, double value_mm, double *value_out)
{
    if (!opt_desc || !value_out)
        return false;

    //Treat "NONE" as mm for backends that omit unit metadata
    if (opt_desc->unit == SANE_UNIT_NONE || opt_desc->unit == SANE_UNIT_MM)
    {
        *value_out = value_mm;
        return true;
    }

    //Not all SANE versions expose every unit enum
#if defined(SANE_UNIT_CM)
    if (opt_desc->unit == SANE_UNIT_CM)
    {
        *value_out = value_mm / 10.0;
        return true;
    }
#endif

    //Some backends expose geometry in inches
    //convert if the unit constant exists
#if defined(SANE_UNIT_INCH)
    if (opt_desc->unit == SANE_UNIT_INCH)
    {
        *value_out = value_mm / 25.4; //25.4 mm per inch
        return true;
    }
#endif

    return false;
}

class SaneScannerBackend : public ScannerBackend
{
public:

    SaneScannerBackend() :
        m_handle(nullptr),
        m_current_document_size(),
        m_initialized(false),
        m_cancel_requested(false),
        m_read_inactivity_timeout_ms(readReadInactivityTimeoutMsFromEnv()),
        m_duplex_option_index(-1)
    {
        g_sane_ref_count++;
        if (g_sane_ref_count == 1)
            enterSaneEnvIfNeeded();

        Debug(QS("SANE: read inactivity timeout=%lld ms (env: QSCAN_SANE_READ_INACTIVITY_TIMEOUT_MS)",
                 (long long)m_read_inactivity_timeout_ms));
    }

    ~SaneScannerBackend() override
    {
        if (m_handle)
        {
            sane_close(m_handle);
            m_handle = nullptr;
        }

        g_sane_ref_count--;
        if (g_sane_ref_count < 0)
            g_sane_ref_count = 0;
        maybeSaneExit();
        leaveSaneEnvIfNeeded();
    }

    bool
    initialize(const QString &device_name) override
    {
        if (m_initialized && m_handle)
            return true;

        if (!ensureSaneInitialized())
            return false;

        Debug(QS("Opening SANE device <%s>", CSTR(device_name)));
        SANE_Status status = sane_open(device_name.toLocal8Bit().constData(), &m_handle);
        if (status != SANE_STATUS_GOOD)
        {
            Debug(QS("sane_open() FAILED: %d (%s)", status, sane_strstatus(status)));
            m_handle = nullptr;
            return false;
        }

        queryCapabilities();
        m_initialized = true;
        return true;
    }

    void
    cancelScan() override
    {
        if (m_handle)
            sane_cancel(m_handle);
        m_cancel_requested.store(true);
    }

    bool
    isOpen() const override
    {
        return m_handle != nullptr;
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
        //Validate backend state
        if (!m_handle)
        {
            error_out = QStringLiteral("Scanner not initialized");
            return false;
        }

        //Apply scan options before starting the SANE pipeline
        //SANE: options must be set before sane_start()
        m_cancel_requested.store(false);
        applyScanParameters(params);
        applyScanArea(params);

        //Multi-page loop: sane_start->readImage->EOF, stop on NO_DOCS or cancel
        //Canonical SANE flow for multi-page ADF is:
        //sane_start() -> sane_read() until EOF -> sane_start() for next page
        int page_number = 0;
        while (true)
        {
            if (m_cancel_requested.load())
                return true;

            //Start next page
            SANE_Status status = sane_start(m_handle);
            if (status == SANE_STATUS_NO_DOCS)
            {
                //ADF empty/end of batch
                break;
            }
            if (status != SANE_STATUS_GOOD)
            {
                error_out = QStringLiteral("Failed to start scan: %1").arg(sane_strstatus(status));
                return false;
            }

            bool cancelled = false;
            bool needs_cancel_cleanup = false;
            QString read_error;

            //Read one page
            QImage image = readImage(&cancelled, &read_error, &needs_cancel_cleanup);

            //Collect effective backend settings for downstream logging/debug
            qscan::ScanPageInfo page_info;
            page_info.backend_kind = QStringLiteral("SANE");
            page_info.backend_details.insert(QStringLiteral("sane.page_number"), page_number);

            if (!image.isNull())
            {
                page_info.backend_details.insert(QStringLiteral("image.width_px"), image.width());
                page_info.backend_details.insert(QStringLiteral("image.height_px"), image.height());
            }

            {
                int dpi = 0;
                if (getOptionInt(SANE_NAME_SCAN_RESOLUTION, dpi))
                {
                    page_info.has_effective_resolution_dpi = true;
                    page_info.effective_resolution_dpi = dpi;
                }

                QString mode;
                if (getOptionString(SANE_NAME_SCAN_MODE, mode))
                {
                    page_info.has_effective_color_mode = true;
                    page_info.effective_color_mode = mode;
                    page_info.backend_details.insert(QStringLiteral("sane.mode"), mode);
                }

                QString src;
                if (getOptionString(SANE_NAME_SCAN_SOURCE, src))
                    page_info.backend_details.insert(QStringLiteral("sane.source"), src);

                SANE_Parameters parms;
                if (sane_get_parameters(m_handle, &parms) == SANE_STATUS_GOOD)
                {
                    page_info.backend_details.insert(QStringLiteral("sane.format"), (int)parms.format);
                    page_info.backend_details.insert(QStringLiteral("sane.depth"), (int)parms.depth);
                    page_info.backend_details.insert(QStringLiteral("sane.pixels_per_line"), (int)parms.pixels_per_line);
                    page_info.backend_details.insert(QStringLiteral("sane.lines"), (int)parms.lines);
                    page_info.backend_details.insert(QStringLiteral("sane.bytes_per_line"), (int)parms.bytes_per_line);
                }
            }

            //Only call sane_cancel() to abort/cleanup on abnormal termination
            if (needs_cancel_cleanup)
                sane_cancel(m_handle);

            if (cancelled || m_cancel_requested.load())
                return true;

            if (image.isNull())
            {
                error_out = read_error.isEmpty() ? QStringLiteral("Failed to read image data") : read_error;
                return false;
            }

            if (!on_page(image, page_number, page_info))
                return true;

            page_number++;

            if (!params.use_adf)
                break;
        }

        return true;
    }

private:

    static qint64
    readReadInactivityTimeoutMsFromEnv()
    {
        //Expose scan stream watchdog as an environment override
        //keeps GUI/backend interface stable while allowing tuning for broken drivers
        static const qint64 kDefaultMs = 60LL * 1000LL;

        const QByteArray raw = qgetenv("QSCAN_SANE_READ_INACTIVITY_TIMEOUT_MS");
        if (raw.isEmpty())
            return kDefaultMs;

        bool ok = false;
        const qlonglong v = QString::fromLocal8Bit(raw).trimmed().toLongLong(&ok);
        if (!ok)
            return kDefaultMs;

        //Allow disabling via <=0 for debugging
        return (qint64)v;
    }

    //SANE state handle,owned by backend
    SANE_Handle m_handle;
    ScanCapabilities m_capabilities;
    QSizeF m_current_document_size;
    bool m_initialized;
    std::atomic<bool> m_cancel_requested;
    qint64 m_read_inactivity_timeout_ms;

    //Cached translation helpers for SANE stringly-typed options
    QStringList m_sane_source_strings;
    QString m_sane_source_flatbed;
    QString m_sane_source_adf_simplex;
    QString m_sane_source_adf_duplex;

    int m_duplex_option_index;
    QString m_duplex_value_on;
    QString m_duplex_value_off;

    static bool
    isAnyTokenContained(const QString &haystack_lower, const QStringList &tokens_lower)
    {
        foreach (const QString &tok, tokens_lower)
        {
            if (haystack_lower.contains(tok))
                return true;
        }
        return false;
    }

    static bool
    isPreferredExact(const QString &value, const QStringList &preferred)
    {
        foreach (const QString &p, preferred)
        {
            if (value.compare(p, Qt::CaseInsensitive) == 0)
                return true;
        }
        return false;
    }

    static void
    upgradeToPreferredExact(QString &current, const QString &candidate, const QStringList &preferred_exact)
    {
        if (candidate.isEmpty())
            return;

        if (current.isEmpty())
        {
            current = candidate;
            return;
        }

        //Prefer canonical names if available
        bool cand_pref = isPreferredExact(candidate, preferred_exact);
        bool curr_pref = isPreferredExact(current, preferred_exact);
        if (cand_pref && !curr_pref)
            current = candidate;
    }

    QString
    selectSaneSourceString(bool use_adf, bool use_duplex) const
    {
        if (!use_adf)
        {
            if (!m_sane_source_flatbed.isEmpty())
                return m_sane_source_flatbed;

            //Fallback: try any flatbed/platen-ish source
            foreach (const QString &s, m_sane_source_strings)
            {
                QString l = s.toLower();
                if (l.contains("flatbed") || l.contains("platen"))
                    return s;
            }
            return QString();
        }

        if (use_duplex && !m_sane_source_adf_duplex.isEmpty())
            return m_sane_source_adf_duplex;

        if (!m_sane_source_adf_simplex.isEmpty())
            return m_sane_source_adf_simplex;

        //Fallback: any ADF/feeder-ish source
        foreach (const QString &s, m_sane_source_strings)
        {
            QString l = s.toLower();
            if (l.contains("adf") || l.contains("feeder") || l.contains("automatic document feeder") || l.contains("document feeder"))
                return s;
        }

        return QString();
    }

    int
    findOptionIndexByName(const char *option_name) const
    {
        if (!m_handle || !option_name)
            return -1;

        //SANE option 0 is meta (option-count), start at 1 and keep scan bounded
        for (int i = 1; i < 100; ++i)
        {
            const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, i);
            if (!opt_desc)
                break;

            if (!opt_desc->name)
                continue;

            if (strcmp(opt_desc->name, option_name) == 0)
                return i;
        }
        return -1;
    }

    bool
    getOptionInt(const char *option_name, int &value_out) const
    {
        if (!m_handle)
            return false;

        const int index = findOptionIndexByName(option_name);
        if (index < 0)
            return false;

        const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, index);
        if (!opt_desc)
            return false;

        if (opt_desc->type == SANE_TYPE_INT)
        {
            SANE_Int v = 0;
            if (sane_control_option(m_handle, index, SANE_ACTION_GET_VALUE, &v, 0) != SANE_STATUS_GOOD)
                return false;
            value_out = (int)v;
            return true;
        }

        if (opt_desc->type == SANE_TYPE_FIXED)
        {
            SANE_Fixed v = 0;
            if (sane_control_option(m_handle, index, SANE_ACTION_GET_VALUE, &v, 0) != SANE_STATUS_GOOD)
                return false;
            value_out = (int)SANE_UNFIX(v);
            return true;
        }

        return false;
    }

    bool
    getOptionString(const char *option_name, QString &value_out) const
    {
        value_out.clear();

        if (!m_handle)
            return false;

        const int index = findOptionIndexByName(option_name);
        if (index < 0)
            return false;

        const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, index);
        if (!opt_desc)
            return false;

        if (opt_desc->type != SANE_TYPE_STRING || opt_desc->size <= 0)
            return false;

        QByteArray buf;
        buf.resize(opt_desc->size + 1);
        memset(buf.data(), 0, (size_t)buf.size());

        if (sane_control_option(m_handle, index, SANE_ACTION_GET_VALUE, buf.data(), 0) != SANE_STATUS_GOOD)
            return false;

        value_out = QString::fromLocal8Bit(buf.constData()).trimmed();
        return !value_out.isEmpty();
    }

    bool
    setOptionValueMm(const char *option_name, double value_mm)
    {
        int index = findOptionIndexByName(option_name);
        if (index < 0)
            return false;

        const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, index);
        if (!opt_desc)
            return false;

        if ((opt_desc->cap & SANE_CAP_SOFT_SELECT) == 0)
            return false;

        double value = 0.0;
        if (!convertLengthMmToSaneUnit(opt_desc, value_mm, &value))
            return false;

        if (opt_desc->type == SANE_TYPE_FIXED)
        {
            SANE_Fixed v = SANE_FIX(value);
            SANE_Status st = sane_control_option(m_handle, index, SANE_ACTION_SET_VALUE, &v, 0);
            if (st != SANE_STATUS_GOOD)
                Debug(QS("SANE set <%s> FAILED: %s", option_name, sane_strstatus(st)));
            return st == SANE_STATUS_GOOD;
        }
        if (opt_desc->type == SANE_TYPE_INT)
        {
            SANE_Int v = (SANE_Int)value;
            SANE_Status st = sane_control_option(m_handle, index, SANE_ACTION_SET_VALUE, &v, 0);
            if (st != SANE_STATUS_GOOD)
                Debug(QS("SANE set <%s> FAILED: %s", option_name, sane_strstatus(st)));
            return st == SANE_STATUS_GOOD;
        }
        return false;
    }

    void
    queryCapabilities()
    {
        //Initialize conservative capabilities defaults without device-specific probing
        m_capabilities = ScanCapabilities();
        m_capabilities.preview_mode = PreviewMode::SingleImage;
        m_capabilities.supports_scan_settings = true;
        m_capabilities.supports_multi_page = false;
        m_capabilities.supports_auto_feed = false;

        if (!m_handle)
            return;

        //Reset cached mappings
        m_sane_source_strings.clear();
        m_sane_source_flatbed.clear();
        m_sane_source_adf_simplex.clear();
        m_sane_source_adf_duplex.clear();
        m_duplex_option_index = -1;
        m_duplex_value_on.clear();
        m_duplex_value_off.clear();

        bool has_scan_area = false;

        //Probe option descriptors and translate backend enums into app capabilities
        //SANE option 0 is meta
        //SANE: hard cap (100) avoids unbounded probing on broken backends
        for (int i = 1; i < 100; ++i)
        {
            const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, i);
            if (!opt_desc)
                break;

            if (!opt_desc->name)
                continue;

            if (strcmp(opt_desc->name, SANE_NAME_SCAN_RESOLUTION) == 0)
            {
                if (opt_desc->type == SANE_TYPE_INT && opt_desc->constraint_type == SANE_CONSTRAINT_RANGE)
                {
                    SANE_Int min_res = opt_desc->constraint.range->min;
                    SANE_Int max_res = opt_desc->constraint.range->max;
                    //Pick common DPI presets within advertised range
                    m_capabilities.supported_resolutions.clear();
                    if (min_res <= 75 && 75 <= max_res) m_capabilities.supported_resolutions << 75;
                    if (min_res <= 150 && 150 <= max_res) m_capabilities.supported_resolutions << 150;
                    if (min_res <= 300 && 300 <= max_res) m_capabilities.supported_resolutions << 300;
                    if (min_res <= 600 && 600 <= max_res) m_capabilities.supported_resolutions << 600;
                    if (min_res <= 1200 && 1200 <= max_res) m_capabilities.supported_resolutions << 1200;
                }
            }
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_MODE) == 0)
            {
                if (opt_desc->type == SANE_TYPE_STRING && opt_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
                {
                    m_capabilities.supported_color_modes.clear();
                    for (int j = 0; opt_desc->constraint.string_list[j] != 0; ++j)
                    {
                        const char *mode_str = opt_desc->constraint.string_list[j];
                        if (strcasecmp(mode_str, "Color") == 0)
                            m_capabilities.supported_color_modes << "Color";
                        else if (strcasecmp(mode_str, "Gray") == 0)
                            m_capabilities.supported_color_modes << "Gray";
                        else if (strcasecmp(mode_str, "Lineart") == 0 || strcasecmp(mode_str, "Binary") == 0)
                            m_capabilities.supported_color_modes << "BW";
                    }
                }
            }
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_SOURCE) == 0)
            {
                if (opt_desc->type == SANE_TYPE_STRING && opt_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
                {
                    //Collect reported source names and build internal mappings
                    m_capabilities.supported_input_sources.clear();
                    m_sane_source_strings.clear();

                    for (int j = 0; opt_desc->constraint.string_list[j] != 0; ++j)
                    {
                        const char *src = opt_desc->constraint.string_list[j];
                        QString srcq = QString::fromLatin1(src);
                        m_capabilities.supported_input_sources << srcq;
                        m_sane_source_strings << srcq;

                        QString lower = srcq.toLower();
                        bool is_flatbed = lower.contains("flatbed") || lower.contains("platen");
                        bool is_adf = lower.contains("automatic document feeder") || lower.contains("document feeder") || lower.contains("adf") || lower.contains("feeder");
                        bool is_duplex = lower.contains("duplex");

                        if (is_flatbed)
                        {
                            //Prefer canonical names when possible
                            upgradeToPreferredExact(m_sane_source_flatbed, srcq, QStringList() << "Flatbed" << "Platen");
                        }

                        if (is_adf && !is_duplex)
                        {
                            upgradeToPreferredExact(m_sane_source_adf_simplex, srcq,
                                                  QStringList() << "Automatic Document Feeder" << "ADF" << "Feeder");
                        }

                        if (is_adf && is_duplex)
                        {
                            upgradeToPreferredExact(m_sane_source_adf_duplex, srcq,
                                                  QStringList() << "ADF Duplex" << "Duplex" << "Automatic Document Feeder Duplex");
                        }

                        //if any source mentions ADF/Feeder, treat device as having an automatic feeder
                        //Many backends use variants like "ADF Front"/"ADF Back" instead of exact "ADF"
                        if (is_adf)
                        {
                            m_capabilities.supports_auto_feed = true;
                            m_capabilities.supports_multi_page = true;
                        }

                        //detect duplex variants in source names (e.g. "ADF Duplex")
                        if (srcq.toLower().contains("duplex"))
                        {
                            m_capabilities.supports_duplex = true;
                            if (m_capabilities.supported_scan_sides.isEmpty())
                                m_capabilities.supported_scan_sides << "Simplex" << "Duplex";
                        }
                    }
                }
            }

            //detect explicit duplex option if present (some backends expose a separate option)
            else if (strstr(opt_desc->name, "duplex") != nullptr || strstr(opt_desc->name, "Duplex") != nullptr)
            {
                m_capabilities.supports_duplex = true;

                //Cache index+values so applyScanParameters can translate clean booleans
                if (m_duplex_option_index < 0)
                    m_duplex_option_index = i;

                if (opt_desc->type == SANE_TYPE_BOOL)
                {
                    if (m_capabilities.supported_scan_sides.isEmpty())
                        m_capabilities.supported_scan_sides << "Simplex" << "Duplex";
                }
                else if (opt_desc->type == SANE_TYPE_STRING && opt_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
                {
                    m_capabilities.supported_scan_sides.clear();
                    for (int j = 0; opt_desc->constraint.string_list[j] != 0; ++j)
                    {
                        QString v = QString::fromLatin1(opt_desc->constraint.string_list[j]);
                        m_capabilities.supported_scan_sides << v;

                        QString lv = v.toLower();
                        if (m_duplex_value_on.isEmpty() && lv.contains("duplex"))
                            m_duplex_value_on = v;
                        if (m_duplex_value_off.isEmpty() && (lv.contains("simplex") || lv.contains("single") || lv.contains("one-sided") || lv.contains("onesided")))
                            m_duplex_value_off = v;
                    }
                }
            }
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_TL_X) == 0 ||
                     strcmp(opt_desc->name, SANE_NAME_SCAN_TL_Y) == 0 ||
                     strcmp(opt_desc->name, SANE_NAME_SCAN_BR_X) == 0 ||
                     strcmp(opt_desc->name, SANE_NAME_SCAN_BR_Y) == 0)
            {
                has_scan_area = true;
            }
        }

        //Best-effort: if scan-area controls exist, allow auto page size
        if (has_scan_area)
            m_capabilities.supports_auto_page_size = true;

        //Fill in fallback presets when backend does not advertise constraints
        if (m_capabilities.supported_resolutions.isEmpty())
            m_capabilities.supported_resolutions << 75 << 150 << 300 << 600;

        if (m_capabilities.supported_color_modes.isEmpty())
            m_capabilities.supported_color_modes << "Color" << "Gray" << "BW";
    }

    void
    applyScanParameters(const ScanParameters &params)
    {
        if (!m_handle)
            return;

        //Translate UI intent (ADF/duplex) into backend-specific source/duplex settings
        bool want_adf = params.use_adf;
        bool want_duplex = params.use_adf && params.use_duplex;
        bool will_set_source_duplex = want_duplex && !m_sane_source_adf_duplex.isEmpty();
        bool duplex_encoded_in_source = will_set_source_duplex;

        const QString desired_source = selectSaneSourceString(want_adf, want_duplex);
        QString applied_source_readback;
        QString applied_duplex_readback;
        bool source_set_attempted = false;
        bool duplex_set_attempted = false;

        //Apply well-known SANE options by name
        //SANE: hard cap (100) avoids unbounded probing on broken backends
        for (int i = 1; i < 100; ++i)
        {
            const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, i);
            if (!opt_desc)
                break;

            if (!opt_desc->name)
                continue;

            if ((opt_desc->cap & SANE_CAP_SOFT_SELECT) == 0)
                continue;

            if (strcmp(opt_desc->name, SANE_NAME_SCAN_RESOLUTION) == 0 && opt_desc->type == SANE_TYPE_INT)
            {
                SANE_Int res = params.resolution;
                SANE_Status st = sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, &res, 0);
                if (st != SANE_STATUS_GOOD)
                    Debug(QS("SANE set resolution=%d FAILED: %s", (int)res, sane_strstatus(st)));
            }
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_MODE) == 0 && opt_desc->type == SANE_TYPE_STRING)
            {
                if (opt_desc->constraint_type != SANE_CONSTRAINT_STRING_LIST)
                    continue;

                const char *target = 0;
                if (params.color_mode == "Color")
                    target = "Color";
                else if (params.color_mode == "Gray")
                    target = "Gray";
                else if (params.color_mode == "BW")
                    target = "Lineart"; //SANE often names BW as Lineart

                if (!target)
                    continue;

                const SANE_String_Const *list = opt_desc->constraint.string_list;
                const char *chosen = 0;
                for (int j = 0; list[j] != 0; ++j)
                {
                    if (strcasecmp(list[j], target) == 0)
                    {
                        chosen = list[j];
                        break;
                    }
                }

                if (!chosen && params.color_mode == "BW")
                {
                    //Some backends use Binary instead of Lineart
                    for (int j = 0; list[j] != 0; ++j)
                    {
                        if (strcasecmp(list[j], "Binary") == 0)
                        {
                            chosen = list[j];
                            break;
                        }
                    }
                }

                if (chosen)
                {
                    //SANE: string options expect writable NUL-terminated buffer
                    char buf[64];
                    strncpy(buf, chosen, sizeof(buf));
                    buf[sizeof(buf) - 1] = 0;
                    SANE_Status st = sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, buf, 0);
                    if (st != SANE_STATUS_GOOD)
                        Debug(QS("SANE set mode=<%s> FAILED: %s", buf, sane_strstatus(st)));
                }
            }
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_SOURCE) == 0 && opt_desc->type == SANE_TYPE_STRING)
            {
                if (opt_desc->constraint_type != SANE_CONSTRAINT_STRING_LIST)
                    continue;

                QString chosen = desired_source;
                if (chosen.isEmpty())
                    continue;

                source_set_attempted = true;

                QByteArray raw = chosen.toLocal8Bit();
                int max_len = (int)opt_desc->size;
                if (max_len <= 1)
                    continue;
                if (raw.size() >= max_len)
                    raw.truncate(max_len - 1);

                QByteArray buf(max_len, '\0');
                memcpy(buf.data(), raw.constData(), raw.size());
                buf[max_len - 1] = '\0';
                SANE_Status st = sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, buf.data(), 0);
                if (st != SANE_STATUS_GOOD)
                    Debug(QS("SANE set source=<%s> FAILED: %s", buf.constData(), sane_strstatus(st)));
                else
                {
                    //Read back the actual value accepted by the backend
                    QByteArray rb(max_len + 1, '\0');
                    SANE_Status gt = sane_control_option(m_handle, i, SANE_ACTION_GET_VALUE, rb.data(), 0);
                    if (gt == SANE_STATUS_GOOD)
                        applied_source_readback = QString::fromLocal8Bit(rb.constData()).trimmed();
                    Debug(QS("SANE set source requested=<%s> effective=<%s>", buf.constData(), CSTR(applied_source_readback)));
                }
            }
            else if (m_duplex_option_index == i)
            {
                //If duplex encoded in source string, avoid double-toggling
                if (will_set_source_duplex)
                    continue;

                duplex_set_attempted = true;

                if (opt_desc->type == SANE_TYPE_BOOL)
                {
                    SANE_Bool v = (want_duplex ? SANE_TRUE : SANE_FALSE);
                    SANE_Status st = sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, &v, 0);
                    if (st != SANE_STATUS_GOOD)
                        Debug(QS("SANE set duplex=%d FAILED: %s", (int)(v == SANE_TRUE), sane_strstatus(st)));
                    else
                    {
                        SANE_Bool rb = v;
                        SANE_Status gt = sane_control_option(m_handle, i, SANE_ACTION_GET_VALUE, &rb, 0);
                        if (gt == SANE_STATUS_GOOD)
                            applied_duplex_readback = (rb == SANE_TRUE) ? QStringLiteral("true") : QStringLiteral("false");
                        Debug(QS("SANE set duplex requested=%d effective=%s", (int)(v == SANE_TRUE), CSTR(applied_duplex_readback)));
                    }
                }
                else if (opt_desc->type == SANE_TYPE_STRING && opt_desc->constraint_type == SANE_CONSTRAINT_STRING_LIST)
                {
                    QString chosen = want_duplex ? m_duplex_value_on : m_duplex_value_off;
                    if (chosen.isEmpty())
                        continue;

                    QByteArray raw = chosen.toLocal8Bit();
                    int max_len = (int)opt_desc->size;
                    if (max_len <= 1)
                        continue;
                    if (raw.size() >= max_len)
                        raw.truncate(max_len - 1);

                    QByteArray buf(max_len, '\0');
                    memcpy(buf.data(), raw.constData(), raw.size());
                    buf[max_len - 1] = '\0';
                    SANE_Status st = sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, buf.data(), 0);
                    if (st != SANE_STATUS_GOOD)
                        Debug(QS("SANE set duplex=<%s> FAILED: %s", buf.constData(), sane_strstatus(st)));
                    else
                    {
                        QByteArray rb(max_len + 1, '\0');
                        SANE_Status gt = sane_control_option(m_handle, i, SANE_ACTION_GET_VALUE, rb.data(), 0);
                        if (gt == SANE_STATUS_GOOD)
                            applied_duplex_readback = QString::fromLocal8Bit(rb.constData()).trimmed();
                        Debug(QS("SANE set duplex requested=<%s> effective=<%s>", buf.constData(), CSTR(applied_duplex_readback)));
                    }
                }
            }
        }

        //Log requested vs effective values to diagnose backend quirks
        Debug(QS("SANE applyScanParameters: want_adf=%d want_duplex=%d desired_source=<%s> effective_source=<%s> duplex_in_source=%d duplex_effective=<%s> source_set=%d duplex_set=%d",
                 (int)want_adf,
                 (int)want_duplex,
                 CSTR(desired_source),
             CSTR(applied_source_readback),
             (int)duplex_encoded_in_source,
             CSTR(applied_duplex_readback),
                 (int)source_set_attempted,
                 (int)duplex_set_attempted));
    }

    void
    applyScanArea(const ScanParameters &params)
    {
        if (!m_handle)
            return;

        //Skip manual scan-area programming when auto page sizing is requested
        if (params.auto_page_size || params.scan_area.isEmpty())
            return;

        //Program TL/BR scan area in mm with unit conversion per option descriptor
        //Some backends use inches; handled in setOptionValueMm
        setOptionValueMm(SANE_NAME_SCAN_TL_X, 0.0);
        setOptionValueMm(SANE_NAME_SCAN_TL_Y, 0.0);
        setOptionValueMm(SANE_NAME_SCAN_BR_X, params.scan_area.width());
        setOptionValueMm(SANE_NAME_SCAN_BR_Y, params.scan_area.height());
    }

    QImage
    readImage(bool *cancelled_out, QString *error_out, bool *needs_cancel_cleanup_out)
    {
        //Read SANE stream into a QImage
        //SANE: some backends stream with unknown lines/bytes_per_line
        if (cancelled_out)
            *cancelled_out = false;
        if (error_out)
            error_out->clear();
        if (needs_cancel_cleanup_out)
            *needs_cancel_cleanup_out = false;

        if (!m_handle)
            return QImage();

        //Fetch stream parameters and compute safe strides for backends with missing metadata
        SANE_Parameters parms;
        SANE_Status status = sane_get_parameters(m_handle, &parms);
        if (status != SANE_STATUS_GOOD)
        {
            Debug(QS("sane_get_parameters() FAILED: %d (%s)", status, sane_strstatus(status)));
            return QImage();
        }

        //Fallback bytes-per-line math for broken backends
        //SANE: some backends report bytes_per_line==0 during streaming
        auto saneBytesPerLineFallback = [](const SANE_Parameters &p) -> int
        {
            if (p.pixels_per_line <= 0)
                return 0;

            if (p.format == SANE_FRAME_GRAY)
            {
                if (p.depth == 1)
                    return (p.pixels_per_line + 7) / 8; //1-bit mono packed: 8 pixels per byte (rounded up)
                if (p.depth == 8)
                    return p.pixels_per_line; //8-bit gray: 1 byte per pixel
                if (p.depth == 16)
                    return p.pixels_per_line * 2; //16-bit gray: 2 bytes per pixel
                return 0;
            }
            if (p.format == SANE_FRAME_RGB)
            {
                if (p.depth == 8)
                    return p.pixels_per_line * 3; //RGB888 packed: 3 bytes per pixel
                if (p.depth == 16)
                    return p.pixels_per_line * 6; //16-bit per channel RGB: 6 bytes per pixel
                return 0;
            }

            return 0;
        };

        //Some backends report bytes_per_line==0 for streaming reads
        int pixels_per_line = parms.pixels_per_line;
        int sane_bytes_per_line = parms.bytes_per_line;
        if (sane_bytes_per_line <= 0)
            sane_bytes_per_line = saneBytesPerLineFallback(parms);

        //Allocate read buffer and reserve roughly one page
        QByteArray buffer;
        if (sane_bytes_per_line > 0)
            buffer.reserve(sane_bytes_per_line * (parms.lines > 0 ? parms.lines : 1024));

        //Stream until EOF with watchdogs for missing EOF and stalled drivers
        //Guard against broken backends that never emit EOF
        //-If expected byte count is known and reached, force page end and cancel cleanup
        //-If backend returns GOOD with no progress for too long, time out and cancel cleanup
        QElapsedTimer inactivity;
        inactivity.start();

        const qint64 inactivity_timeout_ms = m_read_inactivity_timeout_ms;

        const qint64 expected_bytes = (parms.lines > 0 && sane_bytes_per_line > 0)
                                          ? (qint64)sane_bytes_per_line * (qint64)parms.lines
                                          : 0;

        while (true)
        {
            if (m_cancel_requested.load())
            {
                if (cancelled_out)
                    *cancelled_out = true;
                if (needs_cancel_cleanup_out)
                    *needs_cancel_cleanup_out = true;
                return QImage();
            }

            //Read in chunks; 32 KiB keeps overhead low without huge stack buffers
            char chunk[32 * 1024];
            SANE_Int len = 0;
            status = sane_read(m_handle, (SANE_Byte*)chunk, sizeof(chunk), &len);

            if (status == SANE_STATUS_EOF)
                break;

            if (status != SANE_STATUS_GOOD)
            {
                Debug(QS("sane_read() FAILED: %d (%s)", status, sane_strstatus(status)));
                if (error_out)
                    *error_out = QStringLiteral("Failed to read image data: %1").arg(sane_strstatus(status));
                if (needs_cancel_cleanup_out)
                    *needs_cancel_cleanup_out = true;
                return QImage();
            }

            if (len > 0)
            {
                buffer.append(chunk, (int)len);
                inactivity.restart();

                if (expected_bytes > 0 && (qint64)buffer.size() >= expected_bytes)
                {
                    Debug(QS("SANE read: reached expected bytes without EOF: got=%lld expected=%lld; forcing page end",
                             (long long)buffer.size(), (long long)expected_bytes));
                    if (needs_cancel_cleanup_out)
                        *needs_cancel_cleanup_out = true;
                    break;
                }
            }
            else
            {
                if (inactivity_timeout_ms > 0 && inactivity.elapsed() > inactivity_timeout_ms)
                {
                    Debug(QS("SANE read timeout: no progress for %lld ms; forcing cancel",
                             (long long)inactivity_timeout_ms));
                    if (error_out)
                        *error_out = QStringLiteral("Timed out while reading scan data (no progress for %1 ms). You can adjust this via QSCAN_SANE_READ_INACTIVITY_TIMEOUT_MS.")
                                         .arg(inactivity_timeout_ms);
                    if (needs_cancel_cleanup_out)
                        *needs_cancel_cleanup_out = true;
                    return QImage();
                }
            }
        }

        //If lines is unknown, infer from total bytes
        int lines = parms.lines;
        if (lines <= 0 && sane_bytes_per_line > 0)
            lines = buffer.size() / sane_bytes_per_line;

        //Query effective DPI
        //SANE: hard cap (100) avoids unbounded probing on broken backends
        int resolution = 300;
        for (int i = 1; i < 100; ++i)
        {
            const SANE_Option_Descriptor *opt_desc = sane_get_option_descriptor(m_handle, i);
            if (!opt_desc)
                break;
            if (opt_desc->name && strcmp(opt_desc->name, SANE_NAME_SCAN_RESOLUTION) == 0)
            {
                SANE_Int res_value;
                if (sane_control_option(m_handle, i, SANE_ACTION_GET_VALUE, &res_value, 0) == SANE_STATUS_GOOD)
                    resolution = res_value;
                break;
            }
        }

        if (lines <= 0 || pixels_per_line <= 0)
            return QImage();

        //Validate the packed SANE buffer and decode into QImage (stride/padding/bitdepth handling)
        QImage image;
        int dst_pixel_row_bytes = 0;

        if (parms.format == SANE_FRAME_GRAY)
        {
            if (parms.depth == 1 || parms.depth == 8 || parms.depth == 16)
            {
                image = QImage(pixels_per_line, lines, QImage::Format_Grayscale8);
                dst_pixel_row_bytes = pixels_per_line;
            }
        }
        else if (parms.format == SANE_FRAME_RGB)
        {
            if (parms.depth == 8 || parms.depth == 16)
            {
                image = QImage(pixels_per_line, lines, QImage::Format_RGB888);
                dst_pixel_row_bytes = pixels_per_line * 3;
            }
        }

        if (image.isNull() || dst_pixel_row_bytes <= 0)
        {
            Debug(QS("Unsupported SANE image parameters: format=%d depth=%d pixels_per_line=%d lines=%d bytes_per_line=%d",
                     parms.format, parms.depth, pixels_per_line, lines, parms.bytes_per_line));
            return QImage();
        }

        const int src_stride = sane_bytes_per_line;
        if (src_stride <= 0)
        {
            Debug(QS("SANE reported invalid bytes_per_line=%d; cannot decode", parms.bytes_per_line));
            return QImage();
        }

        //Minimum sane row size (packed)
        //if a backend claims smaller stride, decoding would read past rows
        const int min_src_row_bytes = saneBytesPerLineFallback(parms);
        if (min_src_row_bytes > 0 && src_stride < min_src_row_bytes)
        {
            Debug(QS("SANE bytes_per_line too small: %d < %d (format=%d depth=%d)",
                     src_stride, min_src_row_bytes, parms.format, parms.depth));
            return QImage();
        }

        //Expect at least full packed rows in the SANE buffer
        const qint64 required_src_bytes = (qint64)src_stride * (qint64)lines;
        if ((qint64)buffer.size() < required_src_bytes)
        {
            Debug(QS("SANE image buffer short: got=%lld need=%lld (stride=%d lines=%d)",
                     (long long)buffer.size(), (long long)required_src_bytes, src_stride, lines));
            return QImage();
        }

        const int dst_stride = image.bytesPerLine();
        //Qt may pad rows (dst_stride) while SANE returns packed rows
        //copy row-by-row when stride differs
        if (dst_stride != dst_pixel_row_bytes || (min_src_row_bytes > 0 && src_stride != min_src_row_bytes))
        {
            Debug(QS("SANE/Qt stride info: src_stride=%d (reported=%d min=%d) dst_stride=%d dst_row_bytes=%d format=%d depth=%d",
                     src_stride, parms.bytes_per_line, min_src_row_bytes, dst_stride, dst_pixel_row_bytes, parms.format, parms.depth));
        }

        const uchar *src = reinterpret_cast<const uchar*>(buffer.constData());
        uchar *dst = image.bits();

        if (parms.format == SANE_FRAME_GRAY)
        {
            if (parms.depth == 8)
            {
                for (int y = 0; y < lines; ++y)
                {
                    const uchar *src_row = src + y * src_stride;
                    uchar *dst_row = dst + y * dst_stride;
                    memset(dst_row, 0, dst_stride);
                    memcpy(dst_row, src_row, std::min(dst_pixel_row_bytes, src_stride));
                }
            }
            else if (parms.depth == 1)
            {
                for (int y = 0; y < lines; ++y)
                {
                    const uchar *src_row = src + y * src_stride;
                    uchar *dst_row = dst + y * dst_stride;
                    memset(dst_row, 0, dst_stride);
                    for (int x = 0; x < pixels_per_line; ++x)
                    {
                        //SANE 1-bit is usually MSB-first within each byte
                        int byte_index = x / 8;
                        int bit_index = 7 - (x % 8);
                        bool bit = (src_row[byte_index] >> bit_index) & 1;
                        dst_row[x] = bit ? 255 : 0;
                    }
                }
            }
            else if (parms.depth == 16)
            {
                //Downsample 16-bit gray to 8-bit
                //SANE: endian may vary by backend, pick the byte with the stronger signal
                int use_byte = 0;
                {
                    //Sample up to ~2K pixels from first two lines
                    //keeps this fast while usually enough to pick MSB/LSB
                    const int samples = std::min(2048, pixels_per_line * std::min(lines, 2));
                    quint64 sum0 = 0, sum1 = 0;
                    for (int i = 0; i < samples; ++i)
                    {
                        const uchar *p = src + (i * 2);
                        sum0 += p[0];
                        sum1 += p[1];
                    }
                    //4x factor avoids flipping due to noise on near-white scans
                    use_byte = (sum1 > sum0 * 4 ? 1 : 0);
                }

                for (int y = 0; y < lines; ++y)
                {
                    const uchar *src_row = src + y * src_stride;
                    uchar *dst_row = dst + y * dst_stride;
                    memset(dst_row, 0, dst_stride);
                    for (int x = 0; x < pixels_per_line; ++x)
                    {
                        const int idx = x * 2;
                        dst_row[x] = src_row[idx + use_byte];
                    }
                }
            }
        }
        else if (parms.format == SANE_FRAME_RGB)
        {
            if (parms.depth == 8)
            {
                for (int y = 0; y < lines; ++y)
                {
                    const uchar *src_row = src + y * src_stride;
                    uchar *dst_row = dst + y * dst_stride;
                    memset(dst_row, 0, dst_stride);
                    memcpy(dst_row, src_row, std::min(dst_pixel_row_bytes, src_stride));
                }
            }
            else if (parms.depth == 16)
            {
                int use_byte = 0;
                {
                    //Check a few hundred pixels
                    //RGB16 uses 6 bytes per pixel
                    const int pixels_to_check = std::min(512, pixels_per_line * std::min(lines, 2));
                    quint64 sum0 = 0, sum1 = 0;
                    for (int i = 0; i < pixels_to_check; ++i)
                    {
                        const uchar *p = src + (i * 6);
                        //R,G,B each 2 bytes, sum candidate byte across channels
                        sum0 += p[0] + p[2] + p[4];
                        sum1 += p[1] + p[3] + p[5];
                    }
                    //Same 4x safety margin as gray16
                    use_byte = (sum1 > sum0 * 4 ? 1 : 0);
                }

                for (int y = 0; y < lines; ++y)
                {
                    const uchar *src_row = src + y * src_stride;
                    uchar *dst_row = dst + y * dst_stride;
                    memset(dst_row, 0, dst_stride);
                    for (int x = 0; x < pixels_per_line; ++x)
                    {
                        const int si = x * 6;
                        const int di = x * 3;
                        dst_row[di + 0] = src_row[si + 0 + use_byte];
                        dst_row[di + 1] = src_row[si + 2 + use_byte];
                        dst_row[di + 2] = src_row[si + 4 + use_byte];
                    }
                }
            }
        }

        return image;
    }
};

std::unique_ptr<ScannerBackend>
createScannerBackend_SANE()
{
    return std::unique_ptr<ScannerBackend>(new SaneScannerBackend());
}

bool
enumerateDevices_SANE(QList<ScanDeviceInfo> &devices);

QList<ScanDeviceInfo>
enumerateDevices_SANE()
{
    //Collect devices via failure-aware overload
    QList<ScanDeviceInfo> devices;
    enumerateDevices_SANE(devices);
    return devices;
}

static bool
probeSaneBackendsDlopen()
{
    //Probe common SANE backends to detect lib symbol mismatches in AppImage mode
    //Returns true if at least one backend loads successfully
    //
    //Why this check exists: SANE hides backend dlopen failures
    //When libsane-hpaio.so fails to load (e.g. wrong libcrypto version), SANE does not
    //report an error - it silently skips the backend and returns 0 devices with SUCCESS.
    //This makes it impossible to distinguish "no hardware" from "backend lib mismatch".
    //
    //Original error this detects (AppImage bundling conflicting libs):
    //"SANE host setup: dlopen backend <hpaio>: FAILED: /tmp/.mount_QScan-*/usr/lib/libcrypto.so.3: version `OPENSSL_3.3.0' not found (required by /lib64/libssl.so.3)"
    //
    //Root cause: If jpeg/openssl/tiff libs are bundled in AppImage, host SANE backends
    //loaded via dlopen() bind against AppImage libs first (via LD_LIBRARY_PATH),
    //then fail with unresolved versioned symbols:
    //- libsane-hpaio.so: OPENSSL_3.3.0 not found via AppImage libcrypto/libssl
    //- libsane-airscan.so: LIBJPEG_6.2 symbol mismatch via libtiff/libjpeg chain

    const QStringList probe_backends = {QStringLiteral("hpaio"), QStringLiteral("airscan")};
    const QStringList backend_dirs =
    {
        QStringLiteral("/usr/lib/sane"),
        QStringLiteral("/usr/lib64/sane"),
    };

    int probed = 0;
    int loadable = 0;

    for (const QString &backend_name : probe_backends)
    {
        const QString backend_lib = QStringLiteral("libsane-") + backend_name + QStringLiteral(".so");
        QString found_path;

        for (const QString &dir_path : backend_dirs)
        {
            const QDir d(dir_path);
            if (!d.exists())
                continue;

            const QStringList matches = d.entryList(QStringList() << (backend_lib + QStringLiteral("*")), QDir::Files);
            if (!matches.isEmpty())
            {
                found_path = d.filePath(matches[0]);
                break;
            }
        }

        if (found_path.isEmpty())
            continue;

        probed++;
        dlerror();
        void *h = dlopen(found_path.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
        if (h)
        {
            loadable++;
            Debug(QS("SANE dlopen probe: <%s> OK", CSTR(found_path)));
            dlclose(h);
            break;
        }
        else
        {
            const char *err = dlerror();
            Debug(QS("SANE dlopen probe: <%s> FAILED: %s", CSTR(found_path), err ? err : "unknown"));
        }
    }

    if (probed > 0 && loadable == 0)
    {
        Debug(QS("SANE dlopen probe: %d backend(s) probed, none loadable", probed));
        return false;
    }

    if (probed > 0)
        Debug(QS("SANE dlopen probe: OK (%d/%d loadable)", loadable, probed));

    return true;
}

static bool
convertSaneDevicesToList(const SANE_Device **device_list, QList<ScanDeviceInfo> &devices)
{
    //Convert SANE device array to QList
    int count = 0;
    while (device_list[count])
        ++count;

    Debug(QS("sane_get_devices(): %d device(s)", count));

    for (int i = 0; device_list[i]; ++i)
    {
        const SANE_Device *dev = device_list[i];
        QString name = QString::fromLatin1(dev->name);
        QString desc = QString::fromLatin1(dev->model);
        if (!desc.isEmpty() && dev->vendor && dev->vendor[0])
            desc = QString::fromLatin1(dev->vendor) + " " + desc;

        Debug(QS("SANE device [%d]: name=<%s> vendor=<%s> model=<%s> type=<%s>",
                 i,
                 dev->name ? dev->name : "",
                 dev->vendor ? dev->vendor : "",
                 dev->model ? dev->model : "",
                 dev->type ? dev->type : ""));

        devices.append(ScanDeviceInfo(name, desc, ScanDeviceType::SCANNER));
    }

    return true;
}

bool
enumerateDevices_SANE(QList<ScanDeviceInfo> &devices)
{
    //Reset output container
    devices.clear();

    //Log basic SANE env context
    const QByteArray sane_config_dir = qgetenv("SANE_CONFIG_DIR");
    if (!sane_config_dir.isEmpty())
        Debug(QS("SANE_CONFIG_DIR=<%s>", sane_config_dir.constData()));

    //Enter shared SANE init scope
    struct SaneRefGuard
    {
        bool ok;
        SaneRefGuard() : ok(false)
        {
            g_sane_ref_count++;
            if (g_sane_ref_count == 1)
                enterSaneEnvIfNeeded();
            ok = ensureSaneInitialized();
            if (!ok)
            {
                g_sane_ref_count--;
                if (g_sane_ref_count < 0)
                    g_sane_ref_count = 0;
                maybeSaneExit();
                leaveSaneEnvIfNeeded();
            }
        }
        ~SaneRefGuard()
        {
            if (!ok)
                return;
            g_sane_ref_count--;
            if (g_sane_ref_count < 0)
                g_sane_ref_count = 0;
            maybeSaneExit();
            leaveSaneEnvIfNeeded();
        }
    } guard;

    //Abort when SANE init scope failed
    if (!guard.ok)
    {
        Debug(QS("SANE enumeration aborted: sane_init() failed; returning 0 devices"));
        return false;
    }

    //Log host diagnostics without probing
    logSaneHostSetupForDebug();

    //Query device list from SANE backend
    const SANE_Device **device_list = 0;
    SANE_Status status = sane_get_devices(&device_list, SANE_FALSE);

    //Fail on backend query errors
    if (status != SANE_STATUS_GOOD)
    {
        Debug(QS("SANE enumeration FAILED: sane_get_devices() failed: %d (%s); device count unknown", status, sane_strstatus(status)));
        return false;
    }

    //Fail on invalid backend response
    if (!device_list)
    {
        Debug(QS("sane_get_devices() returned GOOD but device_list is null"));
        return false;
    }

    //Check device count
    int count = 0;
    while (device_list[count])
        ++count;

    Debug(QS("sane_get_devices(): %d device(s)", count));

    //When 0 devices returned in AppImage mode, run dlopen probe to detect lib mismatch
    //SANE returns SUCCESS with 0 devices when backends fail to load - this distinguishes
    //that failure mode from "no hardware present"
    if (count == 0)
    {
        const QString appdir = QString::fromLocal8Bit(qgetenv("APPDIR"));
        if (!appdir.isEmpty())
        {
            Debug(QS("SANE enumeration: 0 devices in AppImage mode, running backend dlopen probe"));

            if (!probeSaneBackendsDlopen())
            {
                Debug(QS("SANE enumeration failed: backend dlopen probe failed (lib mismatch likely)"));
                return false;
            }
        }

        Debug(QS("SANE enumeration OK: sane_get_devices() returned 0 devices"));
    }

    //Convert SANE devices to output list
    if (!convertSaneDevicesToList(device_list, devices))
        return false;

    return true;
}
