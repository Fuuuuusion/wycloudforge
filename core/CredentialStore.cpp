#include "CredentialStore.h"

#include <QByteArray>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace core {
namespace {

QString platformError(const QString &action)
{
#ifdef Q_OS_WIN
    return QStringLiteral("%1（Windows 错误 %2）").arg(action).arg(GetLastError());
#else
    return QStringLiteral("%1：当前平台不支持 Windows DPAPI").arg(action);
#endif
}

#ifdef Q_OS_WIN
QByteArray protect(const QByteArray &plain, QString *error)
{
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plain.constData()));
    input.cbData = DWORD(plain.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"WyCloudForge music credential", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error)
            *error = platformError(QStringLiteral("加密凭据失败"));
        return {};
    }
    const QByteArray result(reinterpret_cast<const char *>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return result;
}

QByteArray unprotect(const QByteArray &encrypted, QString *error)
{
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(encrypted.constData()));
    input.cbData = DWORD(encrypted.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        if (error)
            *error = platformError(QStringLiteral("解密凭据失败"));
        return {};
    }
    const QByteArray result(reinterpret_cast<const char *>(output.pbData), int(output.cbData));
    LocalFree(output.pbData);
    return result;
}
#endif

} // namespace

QString CredentialStore::settingsKey(const QString &service)
{
    QString normalized = service.trimmed().toLower();
    normalized.remove(QLatin1Char('/'));
    normalized.remove(QLatin1Char('\\'));
    return QStringLiteral("credentials/%1.dpapi").arg(normalized);
}

QString CredentialStore::read(const QString &service, QString *error)
{
    if (error)
        error->clear();
    const QByteArray encrypted = QSettings().value(settingsKey(service)).toByteArray();
    if (encrypted.isEmpty())
        return {};
#ifdef Q_OS_WIN
    const QByteArray plain = unprotect(encrypted, error);
    return QString::fromUtf8(plain);
#else
    if (error)
        *error = platformError(QStringLiteral("读取凭据失败"));
    return {};
#endif
}

bool CredentialStore::write(const QString &service, const QString &secret, QString *error)
{
    if (secret.isEmpty())
        return remove(service, error);
    if (error)
        error->clear();
#ifdef Q_OS_WIN
    const QByteArray encrypted = protect(secret.toUtf8(), error);
    if (encrypted.isEmpty())
        return false;
    QSettings settings;
    settings.setValue(settingsKey(service), encrypted);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error)
            *error = QStringLiteral("保存加密凭据失败");
        return false;
    }
    QString readError;
    const QString roundTrip = read(service, &readError);
    if (roundTrip != secret) {
        if (error)
            *error = readError.isEmpty() ? QStringLiteral("加密凭据回读校验失败") : readError;
        return false;
    }
    return true;
#else
    if (error)
        *error = platformError(QStringLiteral("保存凭据失败"));
    return false;
#endif
}

bool CredentialStore::remove(const QString &service, QString *error)
{
    if (error)
        error->clear();
    QSettings settings;
    settings.remove(settingsKey(service));
    settings.sync();
    if (settings.status() == QSettings::NoError)
        return true;
    if (error)
        *error = QStringLiteral("删除加密凭据失败");
    return false;
}

bool CredentialStore::migrateLegacy(const QString &service, const QString &legacySettingsKey,
                                    QString *error)
{
    if (error)
        error->clear();
    QString existingError;
    const QString existing = read(service, &existingError);
    if (!existing.isEmpty())
        return true;
    if (!existingError.isEmpty()) {
        if (error)
            *error = existingError;
        return false;
    }

    QSettings settings;
    const QString legacy = settings.value(legacySettingsKey).toString();
    if (legacy.isEmpty())
        return true;
    QString writeError;
    if (!write(service, legacy, &writeError)) {
        if (error)
            *error = writeError;
        return false;
    }
    if (read(service) != legacy) {
        if (error)
            *error = QStringLiteral("旧凭据迁移回读校验失败");
        return false;
    }
    settings.remove(legacySettingsKey);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

} // namespace core
