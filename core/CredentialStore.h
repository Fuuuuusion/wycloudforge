#pragma once

#include <QString>

namespace core {

class CredentialStore
{
public:
    static QString read(const QString &service, QString *error = nullptr);
    static bool write(const QString &service, const QString &secret, QString *error = nullptr);
    static bool remove(const QString &service, QString *error = nullptr);
    static bool migrateLegacy(const QString &service, const QString &legacySettingsKey,
                              QString *error = nullptr);

private:
    static QString settingsKey(const QString &service);
};

} // namespace core
