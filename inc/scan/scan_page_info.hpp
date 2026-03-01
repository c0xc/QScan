#pragma once

#include <QSizeF>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QMetaType>

namespace qscan {

struct ScanPageInfo
{

    QString backend_kind;            //"SANE", "eSCL", "Webcam", "Mobile", ...
    QVariantMap backend_details;     //values explicitly reported by backend

    bool has_effective_resolution_dpi = false;
    int effective_resolution_dpi = 0;

    bool has_effective_color_mode = false;
    QString effective_color_mode;     //backend-reported mode string when available

    bool has_reported_paper_size = false;
    QSizeF reported_paper_size_mm;     //width/height in mm
    QString reported_paper_name;
};

}

Q_DECLARE_METATYPE(qscan::ScanPageInfo)
