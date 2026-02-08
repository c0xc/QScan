#ifndef SETTINGSMANAGER_HPP
#define SETTINGSMANAGER_HPP

#include <cassert>

#include <QApplication>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

/**
 * SettingsManager is a generic accessor for loading and saving
 * a configuration from/to a JSON file.
 * Like QSettings, it can be instantiated and used anywhere,
 * as long as it has enough information to determine the path
 * to the config file. There is a static init routine that can be used
 * to define a custom config path manually.
 * By default, it would use the organization name (from QApplication)
 * to create a directory in ~/.config or wherever config files are stored
 * on your system. This step can be disabled.
 * It uses the application name (from QApplication)
 * to create a config directory for the application.
 * The configuration will be saved in a JSON file.
 */
class SettingsManager //: public QObject
{

public:

    /**
     * Defines the name of the directory to be created
     * above the application config directory.
     * Pass an empty string to prevent this.
     */
    static void
    setOrganizationName(const QString &organization = "");

    /**
     * Defines the name of the config directory.
     */
    static void
    setApplicationName(const QString &application);

    /**
     * Defines a top level key under which any setting will be saved
     * that is accessed with a simple, one-level key (a word without ".").
     */
    static void
    setDefaultGroup(const QString &group_name);

    /**
     * Singleton accessor: Creates and returns global settings object.
     * This can be used if you want to load the config file only once,
     * read its contents multiple times and also modify it
     * without immediately saving it and without keeping a pointer
     * to this settings object, which would have to be passed around
     * between your gui objects.
     */
    static SettingsManager*
    globalInstance();

    /**
     * This constructor creates an instance, which can be used
     * to (re)load settings on-the-fly. The user is expected
     * to save the settings explicitly after modifying them.
     * In order to load the settings once and read them many times,
     * use the static singleton function: globalInstance()
     */
    SettingsManager();
    SettingsManager(const SettingsManager &other);
    //Copy assignment operator for group accessor
    SettingsManager&
    operator=(const SettingsManager &other);

    /**
     * Config path, for example: ~/.config/<application>/
     */
    QDir
    configDirectory(bool create = false) const;

    bool
    setCustomConfigDirectory(const QString &dir_path, bool create = false);

    /**
     * Config file.
     */
    QFileInfo
    configFileInfo() const;

    bool
    hasVariantPrefix() const;

    /**
     * Sets the QVariant prefix, which limits get and set calls
     * to the map with the key "Q".
     * The protected methods fullMap() and setFullMap() are not affected.
     */
    static void
    setInitVariantPrefix(bool use_prefix);
    void
    enableVariantPrefix(bool use_prefix);

    /**
     * Returns a temporary settings manager object for a group.
     * This can be used to access several elements within the same dictionary,
     * without having to specify the name of the dictionary every time.
     * By default, changes on the temporary object must be saved explicitly.
     * If modify_parent is true, the temporary object will directly
     * access and change the data from the parent object,
     * so it would be sufficient to save the parent object.
     *
     * Example:
     * SettingsManager settings;
     * settings.setVariant("magic_number", "3"); //top level element
     * SettingsManager group_settings = settings.groupAccessor("extra", true);
     * group_settings.setVariant("name", "her bert"); //extra.name
     * settings.save(); //saves both changes
     */
    SettingsManager
    groupAccessor(const QString &name, bool modify_parent = false);

    /**
     * Gets value at specified key, where key may contain multiple keys
     * in the form: "subsection.setting1"
     *
     * If default_group is set, any top-level item (without '.' in key)
     * will be put into the default group object.
     * If you want to access certain items directly at the top
     * despite having default_group enabled for everything else,
     * consider reimplementing this method in your child class.
     */
    QVariant
    variant(const QString &key, const QVariant &default_value = QVariant()) const;

    /**
     * Inserts or replaces the value at the specified key,
     * where key is interpreted in the same way as in the getter function.
     * The modified dictionary must exist.
     *
     * If default_group is set, any top-level item (without '.' in key)
     * will be put into the default group object.
     * See note above.
     */
    void
    setVariant(const QString &key, const QVariant &value);

    //QVariant
    //setDefaultVariant(const QString &key, const QVariant &default_value);

    QStringList
    keys(const QString &key = "", bool with_sub_maps = false) const;

    bool
    save();

    void
    replaceWith(const SettingsManager &other);

protected:

    /**
     * Internal getter function for single values,
     * can be called from overloaded getter in child class.
     */
    QVariant
    variant(const QStringList &dict_keys, const QString &item_key, const QVariant &default_value) const;

    void
    setVariant(const QStringList &dict_keys, const QString &item_key, const QVariant &value);

    QVariantMap
    fullMap() const;

    void
    setFullMap(const QVariantMap &value);

    QPair<QStringList, QString>
    splitItemKey(const QString &key, bool use_default_group = true) const;

    QVariantMap
    m_state; //internal settings, do not confuse with m_data!

private:

    QVariantMap
    m_data_obj; //full map with user data loaded from config (local)

    QVariantMap
    *m_data; //pointer to used data map

    bool
    m_using_other_data;

    static QVariantMap&
    stateMap();

};

#endif
