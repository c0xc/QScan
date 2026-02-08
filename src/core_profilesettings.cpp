#include "core/profilesettings.hpp" //TODO namespace / BaseSettings...

QString ProfileSettings::_global_id;

//QString
//ProfileSettings::selectedProfile()
//{
//    return "";
//}

ProfileSettings*
ProfileSettings::profile()
{
    static ProfileSettings global_instance;
    return &global_instance;
}

ProfileSettings*
ProfileSettings::useProfile(const QString& id)
{
    //TODO setPrefix (leave group_name, accessor)
    ProfileSettings *global_instance = profile();
    global_instance->m_state["group_name"] = id;
    return global_instance;
}

ProfileSettings*
ProfileSettings::useDefaultProfile()
{
    QString &default_id = _global_id;
    ProfileSettings settings; //TODO only init once!? settings path may be user-defined
    if (default_id.isEmpty())
    {
        //Find default profile
        foreach (QVariantMap prof, settings.profiles())
        {
            //Use first profile by default, or the one with default=true
            if (default_id.isEmpty()) default_id = prof["id"].toString();
            if (prof.value("default").toBool())
            {
                default_id = prof["id"].toString();
                break;
            }
        }
    }
    if (default_id.isEmpty())
    {
        //Define new profile
        default_id = settings.addProfile("default", QUuid().toString());
    }

    return useProfile(default_id);
}

ProfileSettings::ProfileSettings()
               : SettingsManager(),
                 _id(0)
{
    if (!m_state["default_group_exception"].isValid())
        m_state["default_group_exception"] = QStringList() << "profiles";
}

ProfileSettings::ProfileSettings(const ProfileSettings &other)
               : SettingsManager(other),
                 _id(0)
{
    //if (!m_state["default_group_exception"].isValid())
    //    m_state["default_group_exception"] = QStringList() << "profiles";
    if (!m_state["default_group_exception"].isValid())
        m_state = other.m_state;
}

QString
ProfileSettings::id()
{
    return variant("id").toString();
}

QList<QVariantMap>
ProfileSettings::profiles()
{
    QList<QVariantMap> list;
    foreach (QVariant v, variant("profiles").toList())
    {
        QVariantMap cfg = v.toMap();
        list.append(cfg);
    }
    return list;
}

QString
ProfileSettings::addProfile(const QString &label, const QString &id)
{
    QList<QVariantMap> list = profiles();
    QVariantMap prof;

    QString new_id = id;
    if (new_id.isEmpty())
    {
        QUuid uuid = QUuid::createUuid();
        new_id = uuid.toString();
    }
    prof["label"] = label;
    prof["id"] = new_id;
    prof["path"] = "";

    setVariant("profiles", variant("profiles").toList() << QVariant(prof));
    return new_id;
}

bool
ProfileSettings::removeProfile(const QString &id)
{
    QVariantList list;
    bool found = false;
    foreach (QVariantMap prof, profiles())
    {
        if (prof["id"].toString() == id)
        {
            found = true;
            continue;
        }
        list << prof;
    }
    setVariant("profiles", list);

    return found;
}

