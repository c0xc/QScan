#ifndef PROFILESETTINGS_HPP
#define PROFILESETTINGS_HPP

#include <cassert>

#include <QDebug>
#include <QUuid>
#include <QPointer>

#include "settingsmanager.hpp"

//class ProfileSettings;
class ProfileSettings : public SettingsManager
{

signals:

public:

    //static int
    //selectedProfile();

    static ProfileSettings*
    profile();

    static ProfileSettings*
    useProfile(const QString& id);

    static ProfileSettings*
    useDefaultProfile();

    ProfileSettings();

    ProfileSettings(const ProfileSettings &other);

    QString
    id();

    QList<QVariantMap>
    profiles();

    QString
    addProfile(const QString &label, const QString &id = "");

    bool
    removeProfile(const QString &id);

private:

    int
    _id;

    static QString
    _global_id;

    QPointer<ProfileSettings>
    _global_instance;

};

#endif
