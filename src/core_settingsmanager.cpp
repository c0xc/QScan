#include "core/settingsmanager.hpp" //TODO namespace / BaseSettings...

void
SettingsManager::setOrganizationName(const QString &organization)
{
    QVariantMap &map = stateMap();
    map["organization_name"] = organization;
}

void
SettingsManager::setApplicationName(const QString &application)
{
    QVariantMap &map = stateMap();
    map["application_name"] = application;
}

void
SettingsManager::setDefaultGroup(const QString &group_name)
{
    QVariantMap &map = stateMap();
    map["default_group"] = group_name;
}

SettingsManager*
SettingsManager::globalInstance()
{
    static SettingsManager global_instance;
    return &global_instance;
}

SettingsManager::SettingsManager()
               : m_data(0),
                 m_using_other_data(false)
{
    m_state = stateMap(); //copy global settings

    //Initialize empty data dict
    m_data = &m_data_obj;

    //Load config
    configDirectory(true); //create dir
    QFile file(configFileInfo().filePath());
    if (file.open(QIODevice::ReadOnly))
    {
        QByteArray raw = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(raw);
        QJsonObject j_obj = doc.object();
        *m_data = j_obj.toVariantMap();
    }
    m_state["dirty"] = false;

}

SettingsManager::SettingsManager(const SettingsManager &other)
               : m_state(other.m_state),
                 m_data_obj(*other.m_data),
                 m_using_other_data(false)
{
    //Explicit copy constructor - even though we sometimes want
    //the same behavior that we'd have with the implicit one,
    //but see group accessor for that
    //If a settings manager object is copied implicitly,
    //its (unsaved) user data is copied as well
    //So if the copied object is modified, it has to be saved explicitly
    //The group accessor has a boolean parameter to change that,
    //so that the temporary copy modifies the data of the original object
    m_data = &m_data_obj;
}

SettingsManager&
SettingsManager::operator=(const SettingsManager &other)
{
    if (&other != this)
    {
        m_state = other.m_state; //copy instance state (group accessor)
        m_data_obj = *other.m_data; //copy data map
        if (other.m_using_other_data)
            m_data = other.m_data; //set data map pointer to other/parent's map
        else
            m_data = &m_data_obj; //set data map pointer to local map
    }

    return *this;
}

QDir
SettingsManager::configDirectory(bool create) const
{
    //Determine config path
    QString path_str = m_state.value("config_dir").toString(); //optional
    if (path_str.isEmpty())
    {
        //~/.config/
        QString config_base = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        path_str = config_base;

        //Organization directory (optional)
        if (m_state.contains("organization_name"))
        {
            QString org_name = m_state.value("organization_name").toString();
            if (!org_name.isEmpty())
            {
                QFileInfo fi(QDir(path_str), org_name);
                path_str = fi.filePath();
            }
        }

        //Application directory
        QString app_name = m_state.value("application_name").toString();
        if (app_name.isEmpty())
        {
            //No custom application dir name provided by user
            if (!qApp->applicationName().isEmpty())
            {
                //Use same application name variable also used by QSettings
                app_name = qApp->applicationName();
            }
        }
        if (!app_name.isEmpty())
        {
            //Complete path string
            QFileInfo fi(QDir(path_str), app_name);
            path_str = fi.filePath();
        }
        else
        {
            //Application name missing! Falling back to temp dir!
            //A temporary directory will be created!
            QTemporaryDir temp_dir;
            path_str = temp_dir.path();
        }

        //Save config path
        //m_state["config_dir"] = path_str; //TODO non-const
    }

    //Create directory
    if (create)
    {
        QDir().mkpath(path_str);
    }

    return path_str;
}

bool
SettingsManager::setCustomConfigDirectory(const QString &dir_path, bool create)
{
    QDir dir(dir_path);
    if (create)
    {
        //Create custom config directory
        if (!dir.exists() && !QDir().mkdir(dir_path))
            return false;
    }
    else
    {
        //Use existing config directory
        if (!dir.exists())
            return false;
    }
    QString path_str = m_state.value("config_dir").toString(); //optional

    return true;
}

QFileInfo
SettingsManager::configFileInfo() const
{
    QDir config_dir = configDirectory();
    QString file_name = m_state.value("config_name").toString(); //optional
    //TODO sanity checking: /\?: -
    if (file_name.isEmpty()) file_name = "config.json";
    if (!file_name.endsWith(".json")) file_name += ".json";

    QFileInfo file_info(config_dir, file_name);
    return file_info;
}

bool
SettingsManager::hasVariantPrefix() const
{
    bool use_q_prefix = false;
    if (m_state.contains("use_variant_prefix"))
        use_q_prefix = m_state["use_variant_prefix"].toBool();

    return use_q_prefix;
}

void
SettingsManager::setInitVariantPrefix(bool use_prefix)
{
    QVariantMap &global_state = stateMap(); //global
    global_state["use_variant_prefix"] = use_prefix;
}

void
SettingsManager::enableVariantPrefix(bool use_prefix)
{
    m_state["use_variant_prefix"] = use_prefix;

    if (use_prefix)
    {
        if (!m_data->contains("Q"))
        {
            //Add prefix object
            (*m_data)["Q"] = QVariantMap();
        }
    }
}

SettingsManager
SettingsManager::groupAccessor(const QString &name, bool modify_parent)
{
    //This assignment requires a working copy assignment operator!
    //Otherwise m_data would point to a member
    //of group_accessor, which is limited to the scope of this function
    SettingsManager group_accessor = *this; //copy this object
    if (modify_parent)
    {
        group_accessor.m_data = m_data;
        group_accessor.m_using_other_data = true;
    }
    group_accessor.m_state["group_name"] = name; //set current group
    return group_accessor;
}

QVariant
SettingsManager::variant(const QString &key, const QVariant &default_value) const
{
    //Path to requested dict element
    if (key.isEmpty()) return QVariant(); //don't return full map
    QStringList dict_keys = splitItemKey(key).first;
    QString item_key = splitItemKey(key).second;

    return variant(dict_keys, item_key, default_value);
}

void
SettingsManager::setVariant(const QString &key, const QVariant &value)
{
    //Path to requested dict element
    if (key.isEmpty()) return; //don't update full map (only one element)
    QStringList dict_keys = splitItemKey(key).first;
    QString item_key = splitItemKey(key).second;

    setVariant(dict_keys, item_key, value);
}

//QVariant
//SettingsManager::setDefaultVariant(const QString &key, const QVariant &default_value)
//{
//TODO
//return QVariant();
//}

QStringList
SettingsManager::keys(const QString &key, bool with_sub_maps) const
{
    //Path to requested dict element
    QStringList dict_keys = splitItemKey(key).first;
    QString item_key = splitItemKey(key).second;

    //Get each nested map
    QVariantMap cur_map = *m_data;
    foreach (QString dict_key, dict_keys)
    {
        cur_map = cur_map.value(dict_key).toMap();
    }

    return cur_map.keys();
}

bool
SettingsManager::save()
{
    //Serialize variant map into JSON structure (object at the top)
    QJsonObject j_obj = QJsonObject::fromVariantMap(*m_data);
    QJsonDocument j_doc = QJsonDocument(j_obj);
    //Open config file
    QFile file(configFileInfo().filePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    //Write to file
    QByteArray raw = j_doc.toJson(QJsonDocument::Indented);
    if (file.write(raw) == -1) return false;

    m_state["dirty"] = false;
    return true;
}

void
SettingsManager::replaceWith(const SettingsManager &other)
{
    //Replace our data (dict) with that of other
    //other->m_data points to the data of other
    //we replace our local map and reset our m_data pointer TODO this may not always be good
    m_data_obj = *other.m_data;
    m_data = &m_data_obj; //TODO write to m_data!?
}

QVariant
SettingsManager::variant(const QStringList &dict_keys, const QString &item_key, const QVariant &default_value) const
{
    assert(!item_key.isEmpty());

    //Get each nested map
    QVariantMap cur_map = *m_data;
    foreach (QString dict_key, dict_keys)
    {
        cur_map = cur_map.value(dict_key).toMap();
    }

    if (cur_map.contains(item_key))
        return cur_map.value(item_key);
    else
        return default_value;
}

void
SettingsManager::setVariant(const QStringList &dict_keys, const QString &item_key, const QVariant &value)
{
    assert(!item_key.isEmpty());

    //Get each nested map
    QList<QPair<QString, QVariantMap>> sub_dict_map;
    QVariantMap cur_map = *m_data;
    foreach (QString dict_key, dict_keys)
    {
        cur_map = cur_map.value(dict_key).toMap();
        QPair<QString, QVariantMap> level(dict_key, cur_map);
        sub_dict_map.append(level);
    }
    assert(sub_dict_map.size() == dict_keys.size());

    //Update map at requested level, insert or update map element
    cur_map[item_key] = value;

    //Now, we need to copy each nested dict (again)
    //and put them all together
    //(We would not really have to do that if we'd be limited to one level.)
    //(But having only one level would be boring.)
    //(One level could be simplified to: QMap -> QVariantMap -> QJsonObject)
    //For example, the user might have requested to update the nested
    //dict at [Q, dbs, db1]: Q.dbs.db1.dsn = <dsn>
    //It would be easier if we could get each nested map as reference,
    //use it to get the next map from it and modify that last map:
    //https://stackoverflow.com/q/11090846

    //Recreate nested dictionary from bottom to top
    //mod_dict will be the new dictionary directly in full dict
    QVariantMap mod_dict = cur_map; //copy modified map
    for (int i = sub_dict_map.size() - 1; i > 0; i--)
    {
        //Copy parent dict and insert/update modified dict
        QString dict_key = sub_dict_map[i].first; //current key
        QVariantMap dict = sub_dict_map[i - 1].second; //parent dict
        dict[dict_key] = mod_dict; //put modified dict in parent dict
        mod_dict = dict; //copy updated parent dict for next iteration
    }
    if (!dict_keys.isEmpty())
    {
        //Update dict in full map
        QString mod_dict_key = sub_dict_map[0].first;
        (*m_data)[mod_dict_key] = mod_dict;
    }
    else
    {
        //Update full map
        *m_data = mod_dict;
    }

    m_state["dirty"] = true;
}

QVariantMap
SettingsManager::fullMap() const
{
    return *m_data; //copy of full settings map
}

void
SettingsManager::setFullMap(const QVariantMap &value)
{
    *m_data = value; //replace full settings map
}

QPair<QStringList, QString>
SettingsManager::splitItemKey(const QString &key, bool use_default_group) const
{
    //main.dict.key => [Q, main, dict], key
    QPair<QStringList, QString> pair;

    QStringList dict_keys;
    QString item_key = key;
    //Current group (see group accessor)
    if (m_state.contains("group_name"))
    {
        QString group = m_state["group_name"].toString();
        if (!group.isEmpty())
            dict_keys.append(group);
    }
    //Split requested key at '.' into nested dictionaries
    if (item_key.contains('.')) //or [^\\][/.]
    {
        QStringList sub_keys = item_key.split('.'); //sub_dict.item_key
        item_key = sub_keys.takeLast(); //[sub_dict, item_key]
        foreach (QString k, sub_keys) dict_keys.append(k);
    }
    //Set default group (container object for requested key), if enabled
    if (use_default_group && dict_keys.isEmpty() && !item_key.isEmpty())
    {
        //Top-level item requested (but not empty)
        //Add default group, if configured
        QString group = m_state["default_group"].toString();
        QStringList group_exception = m_state["default_group_exception"].toStringList();
        if (!group.isEmpty() && !group_exception.contains(item_key))
            dict_keys.append(group);
    }
    //Top-level Q prefix, if enabled
    if (hasVariantPrefix())
        dict_keys.prepend("Q");

    pair.first = dict_keys;
    pair.second = item_key;
    return pair;
}

QVariantMap&
SettingsManager::stateMap()
{
    //Creates, returns global map, used to keep config, path etc
    static QVariantMap map;
    return map;
}

