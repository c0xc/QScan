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

#include <QApplication>
#include <QIcon>
#include <QStyle>
#include <QMessageBox>
#include <QUrl>
#include <QVariantMap>
#include <QRegularExpression>
#include <QMenu>
#include <QFrame>
#include <QFont>
#include <QThread>
#include <QPointer>

#include "gui/scannerselector.hpp"
#include "core/profilesettings.hpp"

namespace
{

static QIcon
iconFromThemeOrStyle(const QStringList &theme_names,
                     QStyle *style,
                     QStyle::StandardPixmap fallback,
                     const QString &resource_fallback)
{
    for (const QString &theme_name : theme_names)
    {
        if (theme_name.isEmpty())
            continue;
        QIcon icon = QIcon::fromTheme(theme_name);
        if (!icon.isNull())
            return icon;
    }

    if (style)
    {
        QIcon icon = style->standardIcon(fallback);
        if (!icon.isNull())
            return icon;

        icon = style->standardIcon(QStyle::SP_FileIcon);
        if (!icon.isNull())
            return icon;
    }

    if (QApplication::style())
    {
        QIcon icon = QApplication::style()->standardIcon(fallback);
        if (!icon.isNull())
            return icon;

        icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
        if (!icon.isNull())
            return icon;
    }

    if (!resource_fallback.isEmpty())
    {
        QIcon icon(resource_fallback);
        if (!icon.isNull())
            return icon;
    }

    return QIcon();
}

static QIcon
scannerListIcon(QStyle *style)
{
    return iconFromThemeOrStyle(
        {
            QStringLiteral("document-scanner"),
            QStringLiteral("scanner"),
            QStringLiteral("scanner-flatbed"),
            QStringLiteral("scanner-symbolic"),
        },
        style,
        QStyle::SP_DriveHDIcon,
        QStringLiteral(":/icons/icons/scanner.svg"));
}

static QIcon
cameraListIcon(QStyle *style)
{
    return iconFromThemeOrStyle(
        {
            QStringLiteral("camera-web"),
            QStringLiteral("video-webcam"),
            QStringLiteral("camera-photo"),
            QStringLiteral("camera-video"),
            QStringLiteral("camera"),
        },
        style,
        QStyle::SP_DesktopIcon,
        QStringLiteral(":/icons/icons/webcam.svg"));
}

static QIcon
iconForDeviceListItem(const ScanDeviceInfo &dev, QStyle *style)
{
    if (dev.type == ScanDeviceType::CAMERA)
        return cameraListIcon(style);
    if (dev.type == ScanDeviceType::SCANNER)
        return scannerListIcon(style);
    return iconFromThemeOrStyle({}, style, QStyle::SP_FileIcon, QString());
}

static bool
isNetworkEsclItem(const QListWidgetItem *item)
{
    if (!item)
        return false;
    return item->data(Qt::UserRole).toString().startsWith("escl:");
}

static QListWidgetItem*
addHeaderItem(QListWidget *list, const QString &title)
{
    QListWidgetItem *hdr = new QListWidgetItem(title);
    QFont f = hdr->font();
    f.setBold(true);
    hdr->setFont(f);
    hdr->setFlags(Qt::NoItemFlags);
    list->addItem(hdr);
    return hdr;
}

static QString
lastUsedKeyForDevice(const QString &device_name)
{
    if (device_name.startsWith("escl:"))
        return QStringLiteral("last_used_device_escl");
    return QStringLiteral("last_used_device_sane");
}

static bool
esclSameEndpointIgnoreScheme(const QString &a, const QString &b)
{
    const QUrl ua(a);
    const QUrl ub(b);
    if (!ua.isValid() || !ub.isValid())
        return false;

    const int port_a = ua.port(-1);
    const int port_b = ub.port(-1);
    if (port_a != port_b)
        return false;

    return ua.host().compare(ub.host(), Qt::CaseInsensitive) == 0 && ua.path() == ub.path();
}

} //namespace

ScannerSelector::ScannerSelector(ScanManager *manager, QWidget *parent)
               : QDialog(parent),
                 m_scan_manager(manager)
{
    setWindowTitle(tr("Select Scanner"));
    setupUi();

    populateDeviceList();
    restoreLastSelection();
}

QString
ScannerSelector::selectedDeviceName() const
{
    return m_selected_device;
}

void
ScannerSelector::onDeviceSelectionChanged()
{
    QListWidgetItem *item = m_device_list->currentItem();
    if (item && (item->flags() & Qt::ItemIsSelectable) && !item->data(Qt::UserRole).toString().isEmpty())
    {
        m_selected_device = item->data(Qt::UserRole).toString();
        updateCapabilitiesDisplay();
        m_btn_ok->setEnabled(true);
    }
    else
    {
        m_selected_device.clear();
        m_btn_ok->setEnabled(false);
        m_capabilities_label->clear();
    }
}

void
ScannerSelector::onOkClicked()
{
    //Persist last-used per backend kind (profile-local)
    if (!m_selected_device.isEmpty())
    {
        ProfileSettings *settings = ProfileSettings::useDefaultProfile();
        settings->setVariant(lastUsedKeyForDevice(m_selected_device), m_selected_device);
        settings->save();
    }
    accept();
}

void
ScannerSelector::setupUi()
{
    QVBoxLayout *main_layout = new QVBoxLayout(this);
    
    //Device list
    QLabel *lbl_devices = new QLabel(tr("Available Devices:"));
    main_layout->addWidget(lbl_devices);
    
    m_device_list = new QListWidget;
        m_device_list->setIconSize(QSize(32, 32));
        m_device_list->setMinimumHeight(320);
        m_device_list->setStyleSheet(
            "QListWidget {"
            "  outline: 0px;"
            "}"
            "QListWidget::item {"
            "  border: 1px solid transparent;"
            "  border-radius: 4px;"
            "  padding: 6px 8px;"
            "}"
            "QListWidget::item:disabled {"
            "  border: 1px solid transparent;"
            "  background: transparent;"
            "  color: palette(text);"
            "}"
            "QListWidget::item:!selected:hover {"
            "  border: 1px solid palette(highlight);"
            "  background-color: palette(alternate-base);"
            "}"
            "QListWidget::item:selected {"
            "  background-color: palette(highlight);"
            "  color: palette(highlighted-text);"
            "}"
        );
    main_layout->addWidget(m_device_list);
    connect(m_device_list, SIGNAL(currentRowChanged(int)),
            this, SLOT(onDeviceSelectionChanged()));
    connect(m_device_list, SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this, SLOT(onOkClicked()));
    
    //Capabilities display
    m_capabilities_label = new QLabel;
    m_capabilities_label->setWordWrap(true);
    m_capabilities_label->setStyleSheet("QLabel { color: #666; font-size: 10pt; }");
    main_layout->addWidget(m_capabilities_label);

    //Manual source management
    QHBoxLayout *source_row = new QHBoxLayout;
    source_row->addStretch();

    m_btn_add_source = new QToolButton;
    m_btn_add_source->setText(tr("Add Source…"));
    m_btn_add_source->setPopupMode(QToolButton::InstantPopup);
    QMenu *menu = new QMenu(m_btn_add_source);
    QAction *act_add_escl = menu->addAction(tr("AirScan (eSCL)…"));
    connect(act_add_escl, &QAction::triggered, this, &ScannerSelector::onAddEsclSourceRequested);
    m_btn_add_source->setMenu(menu);
    source_row->addWidget(m_btn_add_source);

    m_btn_remove_source = new QPushButton(tr("Remove"));
    connect(m_btn_remove_source, SIGNAL(clicked()), this, SLOT(onRemoveNetworkScannerClicked()));
    source_row->addWidget(m_btn_remove_source);

    main_layout->addLayout(source_row);

    //eSCL add panel (hidden by default)
    m_escl_panel = new QFrame;
    static_cast<QFrame*>(m_escl_panel)->setFrameShape(QFrame::StyledPanel);
    QVBoxLayout *escl_layout = new QVBoxLayout(m_escl_panel);

    QLabel *lbl_escl = new QLabel(tr("Add AirScan (eSCL) scanner"));
    escl_layout->addWidget(lbl_escl);

    QHBoxLayout *escl_url_row = new QHBoxLayout;
    m_escl_url_edit = new QLineEdit;
    m_escl_url_edit->setPlaceholderText(tr("Host or IP (e.g. scanner.local or 192.168.1.25)"));
    escl_url_row->addWidget(m_escl_url_edit);
    m_btn_escl_test = new QPushButton(tr("Test"));
    connect(m_btn_escl_test, SIGNAL(clicked()), this, SLOT(onEsclTestClicked()));
    escl_url_row->addWidget(m_btn_escl_test);
    escl_layout->addLayout(escl_url_row);

    m_escl_status = new QLabel;
    m_escl_status->setWordWrap(true);
    escl_layout->addWidget(m_escl_status);

    m_escl_label_edit = new QLineEdit;
    m_escl_label_edit->setPlaceholderText(tr("Display name (optional)"));
    m_escl_label_edit->setVisible(false);
    escl_layout->addWidget(m_escl_label_edit);

    QHBoxLayout *escl_btn_row = new QHBoxLayout;
    escl_btn_row->addStretch();
    m_btn_escl_save = new QPushButton(tr("Save"));
    m_btn_escl_save->setEnabled(false);
    connect(m_btn_escl_save, SIGNAL(clicked()), this, SLOT(onEsclSaveClicked()));
    escl_btn_row->addWidget(m_btn_escl_save);
    m_btn_escl_cancel = new QPushButton(tr("Cancel"));
    connect(m_btn_escl_cancel, SIGNAL(clicked()), this, SLOT(onEsclCancelClicked()));
    escl_btn_row->addWidget(m_btn_escl_cancel);
    escl_layout->addLayout(escl_btn_row);

    m_escl_panel->setVisible(false);
    main_layout->addWidget(m_escl_panel);
    
    //Buttons
    QHBoxLayout *button_layout = new QHBoxLayout;
    button_layout->addStretch();
    
    m_btn_ok = new QPushButton(tr("OK"));
    m_btn_ok->setEnabled(false);
    m_btn_ok->setDefault(true);
    connect(m_btn_ok, SIGNAL(clicked()), this, SLOT(onOkClicked()));
    button_layout->addWidget(m_btn_ok);
    
    m_btn_cancel = new QPushButton(tr("Cancel"));
    connect(m_btn_cancel, SIGNAL(clicked()), this, SLOT(reject()));
    button_layout->addWidget(m_btn_cancel);
    
    main_layout->addLayout(button_layout);
    
    resize(680, 520);
}

void
ScannerSelector::populateDeviceList()
{
    m_device_list->clear();

    QList<ScanDeviceInfo> scanners;
    QList<ScanDeviceInfo> cameras;
    QList<ScanDeviceInfo> other;

    QList<ScanDeviceInfo> devices = m_scan_manager ? m_scan_manager->availableDevices() : QList<ScanDeviceInfo>();
    foreach (const ScanDeviceInfo &dev, devices)
    {
        if (dev.type == ScanDeviceType::SCANNER)
            scanners << dev;
        else if (dev.type == ScanDeviceType::CAMERA)
            cameras << dev;
        else
            other << dev;
    }

    bool any_selectable = false;

    if (!scanners.isEmpty())
    {
        addHeaderItem(m_device_list, tr("Scanners"));
        foreach (const ScanDeviceInfo &dev, scanners)
        {
            const bool is_escl = dev.name.startsWith("escl:");
            const QString display = dev.description;
            const QString tag = is_escl ? QStringLiteral("eSCL") : QStringLiteral("SANE");

            QString address = dev.name;
            if (is_escl)
                address = address.mid(5);

            //Keep the list visually clean; expose address/device id on hover
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(tag, display));
            item->setData(Qt::UserRole, dev.name);
            item->setData(Qt::UserRole + 1, (int)dev.type);

            if (is_escl)
                item->setToolTip(tr("AirScan (eSCL)\nAddress: %1").arg(address));
            else
                item->setToolTip(tr("SANE\nDevice: %1").arg(dev.name));

            if (!dev.selectable)
                item->setFlags(Qt::NoItemFlags);
            else
                any_selectable = true;

            //Use a single scanner icon so eSCL/SANE entries look consistent in size
            item->setIcon(iconForDeviceListItem(dev, style()));

            m_device_list->addItem(item);
        }
    }

    if (!cameras.isEmpty())
    {
        addHeaderItem(m_device_list, tr("Cameras"));
        foreach (const ScanDeviceInfo &dev, cameras)
        {
            const QString display = dev.description;

            //Keep the list simple; show dev id on hover
            QListWidgetItem *item = new QListWidgetItem(QStringLiteral("%1 · %2").arg(QStringLiteral("CAM"), display));
            item->setData(Qt::UserRole, dev.name);
            item->setData(Qt::UserRole + 1, (int)dev.type);
            item->setIcon(iconForDeviceListItem(dev, style()));

            item->setToolTip(tr("Device: %1").arg(dev.name));

            if (!dev.selectable)
                item->setFlags(Qt::NoItemFlags);
            else
                any_selectable = true;
            m_device_list->addItem(item);
        }
    }

    if (m_device_list->count() == 0)
    {
        QListWidgetItem *none = new QListWidgetItem(tr("No devices found yet.\nUse \"Add Source…\" to add an AirScan (eSCL) scanner."));
        none->setFlags(Qt::NoItemFlags);
        none->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
        m_device_list->addItem(none);
        return;
    }

    if (!any_selectable)
    {
        QListWidgetItem *none = new QListWidgetItem(tr("No usable devices found yet.\nUse \"Add Source…\" to add an AirScan (eSCL) scanner."));
        none->setFlags(Qt::NoItemFlags);
        none->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
        m_device_list->addItem(none);
    }
}

void
ScannerSelector::restoreLastSelection()
{
    //Prefer last-used sane/escl if available; fall back to first entry
    ProfileSettings *settings = ProfileSettings::useDefaultProfile();
    const QString last_sane = settings->variant("last_used_device_sane").toString();
    const QString last_escl = settings->variant("last_used_device_escl").toString();

    auto trySelectByName = [this](const QString &name) -> bool
    {
        if (name.isEmpty())
            return false;
        for (int i = 0; i < m_device_list->count(); ++i)
        {
            QListWidgetItem *it = m_device_list->item(i);
            if (it && it->data(Qt::UserRole).toString() == name)
            {
                m_device_list->setCurrentRow(i);
                onDeviceSelectionChanged();
                return true;
            }
        }
        return false;
    };

    if (trySelectByName(last_sane))
        return;
    if (trySelectByName(last_escl))
        return;

    if (m_device_list->count() > 0)
    {
        //Pick first selectable item
        for (int i = 0; i < m_device_list->count(); ++i)
        {
            QListWidgetItem *it = m_device_list->item(i);
            if (it && (it->flags() & Qt::ItemIsSelectable) && !it->data(Qt::UserRole).toString().isEmpty())
            {
                m_device_list->setCurrentRow(i);
                onDeviceSelectionChanged();
                break;
            }
        }
    }
}

void
ScannerSelector::onAddEsclSourceRequested()
{
    m_escl_panel->setVisible(true);
    m_escl_url_edit->clear();
    m_escl_status->clear();
    m_escl_label_edit->clear();
    m_escl_label_edit->setVisible(false);
    m_btn_escl_save->setEnabled(false);
    m_escl_tested_url.clear();
    m_escl_url_edit->setFocus();
}

void
ScannerSelector::onEsclTestClicked()
{
    if (!m_scan_manager)
        return;

    const QString input = m_escl_url_edit->text();

    m_escl_status->setText(tr("Testing…"));
    m_btn_escl_test->setEnabled(false);
    m_btn_escl_save->setEnabled(false);
    m_escl_label_edit->setVisible(false);
    m_escl_tested_url.clear();

    QPointer<ScannerSelector> self(this);
    ScanManager *mgr = m_scan_manager;
    QThread *t = QThread::create([self, mgr, input]()
    {
        QString normalized;
        QString probe_err;
        QString suggested_label;
        const bool ok = mgr->testEsclEndpoint(input, normalized, probe_err, suggested_label);
        if (!self)
            return;

        QMetaObject::invokeMethod(self, [self, ok, normalized, probe_err, suggested_label]()
        {
            if (!self)
                return;

            self->m_btn_escl_test->setEnabled(true);

            if (!ok)
            {
                const QString msg = probe_err.isEmpty() ? QObject::tr("Connection failed") : probe_err;
                self->m_escl_status->setText(msg);
                self->m_escl_tested_url.clear();
                self->m_btn_escl_save->setEnabled(false);
                return;
            }

            self->m_escl_status->setText(QObject::tr("Connected."));
            self->m_escl_tested_url = normalized;
            self->m_escl_label_edit->setVisible(true);
            self->m_btn_escl_save->setEnabled(true);

            if (!suggested_label.isEmpty() && self->m_escl_label_edit->text().trimmed().isEmpty())
                self->m_escl_label_edit->setText(suggested_label);

            //Show canonical URL back to the user
            self->m_escl_url_edit->setText(normalized);
        }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void
ScannerSelector::onEsclSaveClicked()
{
    if (m_escl_tested_url.isEmpty())
        return;

    ProfileSettings *settings = ProfileSettings::useDefaultProfile();
    const QString label = m_escl_label_edit->text().trimmed();

    QVariantList list = settings->variant("network_scanners").toList();
    for (const QVariant &v : list)
    {
        QVariantMap m = v.toMap();
        if (m.value("url").toString() == m_escl_tested_url)
        {
            QMessageBox::information(this, tr("Already Added"), tr("This network scanner is already saved."));
            m_escl_panel->setVisible(false);
            return;
        }
    }

    //If the same endpoint exists with a different scheme (http vs https), update it
    //TODO this might not be needed (standard http entrypoint redirects to https)
    for (int i = 0; i < list.size(); ++i)
    {
        QVariantMap m = list[i].toMap();
        const QString existing = m.value("url").toString();
        if (existing.isEmpty())
            continue;

        if (esclSameEndpointIgnoreScheme(existing, m_escl_tested_url))
        {
            m["url"] = m_escl_tested_url;
            if (!label.isEmpty())
                m["label"] = label;
            m["ignore_cert_error"] = true;
            list[i] = m;
            settings->setVariant("network_scanners", list);
            settings->save();

            m_escl_panel->setVisible(false);

            if (m_scan_manager)
                m_scan_manager->initialize();
            populateDeviceList();
            restoreLastSelection();

            return;
        }
    }

    QVariantMap entry;
    entry["label"] = label;
    entry["url"] = m_escl_tested_url;
    entry["ignore_cert_error"] = true;
    list << entry;

    settings->setVariant("network_scanners", list);
    settings->save();

    m_escl_panel->setVisible(false);

    //Re-enumerate to make ScanManager aware of new source device
    if (m_scan_manager)
        m_scan_manager->initialize();
    populateDeviceList();
    restoreLastSelection();

    //Select the newly-added endpoint
    const QString target_name = QStringLiteral("escl:") + m_escl_tested_url;
    for (int i = 0; i < m_device_list->count(); ++i)
    {
        const QVariant name = m_device_list->item(i)->data(Qt::UserRole);
        if (name.toString() == target_name)
        {
            m_device_list->setCurrentRow(i);
            break;
        }
    }
}

void
ScannerSelector::onEsclCancelClicked()
{
    m_escl_panel->setVisible(false);
}

void
ScannerSelector::onRemoveNetworkScannerClicked()
{
    QListWidgetItem *item = m_device_list->currentItem();
    if (!isNetworkEsclItem(item))
    {
        QMessageBox::information(this, tr("Remove"), tr("Select a network scanner entry to remove."));
        return;
    }

    const QString device_name = item->data(Qt::UserRole).toString();
    const QString url = device_name.mid(QStringLiteral("escl:").size());

    ProfileSettings *settings = ProfileSettings::useDefaultProfile();
    QVariantList list = settings->variant("network_scanners").toList();
    QVariantList out;
    bool removed = false;
    for (const QVariant &v : list)
    {
        QVariantMap m = v.toMap();
        if (m.value("url").toString() == url)
        {
            removed = true;
            continue;
        }
        out << m;
    }

    if (!removed)
        return;

    settings->setVariant("network_scanners", out);

    //If last-used points at this device, clear it
    const QString last_escl = settings->variant("last_used_device_escl").toString();
    if (last_escl == device_name)
        settings->setVariant("last_used_device_escl", "");

    settings->save();

    if (m_scan_manager)
        m_scan_manager->initialize();
    populateDeviceList();
    restoreLastSelection();
}

void
ScannerSelector::updateCapabilitiesDisplay()
{
    QListWidgetItem *item = m_device_list->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsSelectable) || item->data(Qt::UserRole).toString().isEmpty())
    {
        m_capabilities_label->clear();
        return;
    }
    
    ScanDeviceType type = (ScanDeviceType)item->data(Qt::UserRole + 1).toInt();
    
    QString info;
    if (type == ScanDeviceType::SCANNER)
    {
        if (item->data(Qt::UserRole).toString().startsWith("escl:"))
        {
            info = tr("AirScan (eSCL) scanner selected. Click OK to continue.");
            m_capabilities_label->setText(info);
            return;
        }

        //NOTE We show the scanner address in addition to its name/label,
        //so the user can distinguish two entries where only one
        //uses the hostname for lookup on the LAN
        //NOTE Device capabilities are not available yet (before probe/init)
        //Opening an HP scanner device can fail if the hplip plugin is not up to date
        //In this case, hplip opens a dialog titled:
        //"Driver Plug-in Installation is required"
        //In this case, the user must follow those instructions, hoping
        //the download won't fail because of HP or a network issue
        //like a corporate proxy
        //(Scanning via eSCL can be a workaround in this case.)

        info = tr("Scanner device selected. Click OK to continue.");
    }
    else if (type == ScanDeviceType::CAMERA)
    {
        info = tr("Camera device selected. Click OK to continue.");
    }
    else
    {
        info = tr("Device selected. Click OK to continue.");
    }

    m_capabilities_label->setText(info);
}
