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

#include "scan/scanner_backend.hpp"
#include "scan/scan_device_info.hpp"
#include "core/classlogger.hpp"

#include <sane/sane.h>
#include <sane/saneopts.h>

#include <QByteArray>
#include <QImage>
#include <atomic>
#include <cstring>
#include <memory>
#include <strings.h>

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

static bool
ensureSaneInitialized()
{
    if (g_sane_initialized)
        return true;

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
maybeSaneExit()
{
    if (g_sane_ref_count == 0 && g_sane_initialized)
    {
        Debug(QS("Last SANE backend destroyed, calling sane_exit()"));
        sane_exit();
        g_sane_initialized = false;
    }
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

    //We treat "NONE" as mm for the common backends that omit unit metadata.
    if (opt_desc->unit == SANE_UNIT_NONE || opt_desc->unit == SANE_UNIT_MM)
    {
        *value_out = value_mm;
        return true;
    }

    //Not all SANE versions expose every unit enum.
#if defined(SANE_UNIT_CM)
    if (opt_desc->unit == SANE_UNIT_CM)
    {
        *value_out = value_mm / 10.0;
        return true;
    }
#endif

    //Some backends expose geometry in inches; convert if the constant exists.
#if defined(SANE_UNIT_INCH)
    if (opt_desc->unit == SANE_UNIT_INCH)
    {
        *value_out = value_mm / 25.4;
        return true;
    }
#endif

    return false;
}

class SaneScannerBackend : public ScannerBackend
{
public:

    SaneScannerBackend()
        : m_handle(nullptr),
          m_current_document_size(),
          m_initialized(false),
          m_cancel_requested(false)
    {
        g_sane_ref_count++;
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
    scan(const ScanParameters &params, const PageCallback &on_page, QString &error_out) override
    {
        if (!m_handle)
        {
            error_out = QStringLiteral("Scanner not initialized");
            return false;
        }

        m_cancel_requested.store(false);
        applyScanParameters(params);
        applyScanArea(params);

        int page_number = 0;
        while (true)
        {
            if (m_cancel_requested.load())
                return true;

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
            QImage image = readImage(&cancelled);
            sane_cancel(m_handle);

            if (cancelled || m_cancel_requested.load())
                return true;

            if (image.isNull())
            {
                error_out = QStringLiteral("Failed to read image data");
                return false;
            }

            if (!on_page(image, page_number))
                return true;

            page_number++;

            if (!params.use_adf)
                break;
        }

        return true;
    }

private:

    //SANE state handle,owned by backend
    SANE_Handle m_handle;
    ScanCapabilities m_capabilities;
    QSizeF m_current_document_size;
    bool m_initialized;
    std::atomic<bool> m_cancel_requested;

    int
    findOptionIndexByName(const char *option_name) const
    {
        if (!m_handle || !option_name)
            return -1;

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
            return sane_control_option(m_handle, index, SANE_ACTION_SET_VALUE, &v, 0) == SANE_STATUS_GOOD;
        }
        if (opt_desc->type == SANE_TYPE_INT)
        {
            SANE_Int v = (SANE_Int)value;
            return sane_control_option(m_handle, index, SANE_ACTION_SET_VALUE, &v, 0) == SANE_STATUS_GOOD;
        }
        return false;
    }

    void
    queryCapabilities()
    {
        m_capabilities = ScanCapabilities();
        m_capabilities.preview_mode = PreviewMode::SingleImage;
        m_capabilities.supports_scan_settings = true;
        m_capabilities.supports_multi_page = false;
        m_capabilities.supports_auto_feed = false;

        if (!m_handle)
            return;

        bool has_scan_area = false;

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
                    if (stringListContains(opt_desc->constraint.string_list, "Automatic Document Feeder") ||
                        stringListContains(opt_desc->constraint.string_list, "ADF"))
                    {
                        m_capabilities.supports_auto_feed = true;
                        m_capabilities.supports_multi_page = true;
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

        //Best-effort: if the backend exposes scan-area controls, allow the GUI to offer auto page size.
        if (has_scan_area)
            m_capabilities.supports_auto_page_size = true;

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
                sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, &res, 0);
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
                    target = "Lineart";

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
                    char buf[64];
                    strncpy(buf, chosen, sizeof(buf));
                    buf[sizeof(buf) - 1] = 0;
                    sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, buf, 0);
                }
            }
            else if (strcmp(opt_desc->name, SANE_NAME_SCAN_SOURCE) == 0 && opt_desc->type == SANE_TYPE_STRING)
            {
                if (!params.use_adf)
                    continue;
                if (opt_desc->constraint_type != SANE_CONSTRAINT_STRING_LIST)
                    continue;

                const SANE_String_Const *list = opt_desc->constraint.string_list;
                const char *chosen = 0;
                for (int j = 0; list[j] != 0; ++j)
                {
                    if (strcmp(list[j], "Automatic Document Feeder") == 0 || strcmp(list[j], "ADF") == 0)
                    {
                        chosen = list[j];
                        break;
                    }
                }

                if (chosen)
                {
                    char buf[64];
                    strncpy(buf, chosen, sizeof(buf));
                    buf[sizeof(buf) - 1] = 0;
                    sane_control_option(m_handle, i, SANE_ACTION_SET_VALUE, buf, 0);
                }
            }
        }
    }

    void
    applyScanArea(const ScanParameters &params)
    {
        if (!m_handle)
            return;

        //If auto page size requested or scan_area is empty, let the backend decide.
        if (params.auto_page_size || params.scan_area.isEmpty())
            return;

        //Set full-area from 0,0 to width,height (in mm). Some backends use inches; handled in setOptionValueMm.
        setOptionValueMm(SANE_NAME_SCAN_TL_X, 0.0);
        setOptionValueMm(SANE_NAME_SCAN_TL_Y, 0.0);
        setOptionValueMm(SANE_NAME_SCAN_BR_X, params.scan_area.width());
        setOptionValueMm(SANE_NAME_SCAN_BR_Y, params.scan_area.height());
    }

    QImage
    readImage(bool *cancelled_out)
    {
        if (cancelled_out)
            *cancelled_out = false;

        if (!m_handle)
            return QImage();

        SANE_Parameters parms;
        SANE_Status status = sane_get_parameters(m_handle, &parms);
        if (status != SANE_STATUS_GOOD)
        {
            Debug(QS("sane_get_parameters() FAILED: %d (%s)", status, sane_strstatus(status)));
            return QImage();
        }

        int bytes_per_line = parms.bytes_per_line;
        int pixels_per_line = parms.pixels_per_line;

        QByteArray buffer;
        buffer.reserve(bytes_per_line * (parms.lines > 0 ? parms.lines : 1024));

        while (true)
        {
            if (m_cancel_requested.load())
            {
                if (cancelled_out)
                    *cancelled_out = true;
                return QImage();
            }

            char chunk[32 * 1024];
            SANE_Int len = 0;
            status = sane_read(m_handle, (SANE_Byte*)chunk, sizeof(chunk), &len);

            if (status == SANE_STATUS_EOF)
                break;

            if (status != SANE_STATUS_GOOD)
            {
                Debug(QS("sane_read() FAILED: %d (%s)", status, sane_strstatus(status)));
                return QImage();
            }

            if (len > 0)
                buffer.append(chunk, (int)len);
        }

        int lines = parms.lines;
        if (lines <= 0 && bytes_per_line > 0)
            lines = buffer.size() / bytes_per_line;

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

        if (resolution > 0 && pixels_per_line > 0 && lines > 0)
        {
            double width_mm = (pixels_per_line * 25.4) / resolution;
            double height_mm = (lines * 25.4) / resolution;
            m_current_document_size = QSizeF(width_mm, height_mm);
        }

        if (lines <= 0 || pixels_per_line <= 0)
            return QImage();

        QImage image;
        if (parms.format == SANE_FRAME_GRAY)
        {
            image = QImage(pixels_per_line, lines, QImage::Format_Grayscale8);
        }
        else if (parms.format == SANE_FRAME_RGB)
        {
            image = QImage(pixels_per_line, lines, QImage::Format_RGB888);
        }
        else
        {
            Debug(QS("Unsupported SANE frame format: %d", parms.format));
            return QImage();
        }

        int expected = image.sizeInBytes();
        if (buffer.size() < expected)
            return QImage();

        memcpy(image.bits(), buffer.constData(), expected);
        return image;
    }
};

std::unique_ptr<ScannerBackend>
createScannerBackend_SANE()
{
    return std::unique_ptr<ScannerBackend>(new SaneScannerBackend());
}

QList<ScanDeviceInfo>
enumerateDevices_SANE()
{
    QList<ScanDeviceInfo> devices;

    struct SaneRefGuard
    {
        bool ok;
        SaneRefGuard() : ok(false)
        {
            g_sane_ref_count++;
            ok = ensureSaneInitialized();
            if (!ok)
            {
                g_sane_ref_count--;
                if (g_sane_ref_count < 0)
                    g_sane_ref_count = 0;
                maybeSaneExit();
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
        }
    } guard;

    if (!guard.ok)
        return devices;

    const SANE_Device **device_list = 0;
    SANE_Status status = sane_get_devices(&device_list, SANE_FALSE);
    if (status == SANE_STATUS_GOOD && device_list)
    {
        for (int i = 0; device_list[i]; ++i)
        {
            const SANE_Device *dev = device_list[i];
            QString name = QString::fromLatin1(dev->name);
            QString desc = QString::fromLatin1(dev->model);
            if (!desc.isEmpty() && dev->vendor && dev->vendor[0])
                desc = QString::fromLatin1(dev->vendor) + " " + desc;

            devices.append(ScanDeviceInfo(name, desc, ScanDeviceType::SCANNER));
        }
    }
    return devices;
}
