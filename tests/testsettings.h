#pragma once

#include <QSettings>
#include <QTemporaryDir>

#include "config/SecretStore.h"
#include "config/SettingsStore.h"
#include "core/ConnectionRole.h"

// Isolates QSettings for a test process.
//
// SettingsStore::makeSettings() falls back to the real application identity
// ("llocr" / "LLM OCR") when the test process has not set one, so every bare
// QSettings() in a test would read and write the user's actual profile (the
// Windows registry). Tests that redirect the runtime (runtime/rootDir,
// runtime/modelsDir, runtime/serverPath) or the launch settings
// (launch/modelPath, …) then overwrite the user's real values with paths
// inside a QTemporaryDir that is deleted when the test ends — after which the
// application "forgets" the installed runtime and models on every start.
//
// This guard redirects the default QSettings() constructor to an INI file
// inside a private temp directory that is removed together with the guard, so
// a test process can never touch the real profile.
//
// The secrets live outside QSettings — in the OS secret store (ADR 227). The
// guard gives the test process its own secret namespace, so a test never
// writes into (or deletes from) the app's own keychain entries.
//
// Declare it as the FIRST data member of the test class: it must be
// constructed before any SettingsStore and outlive every test function
// (QTEST_MAIN creates the test object once, before running all slots).
class TestSettingsIsolation
{
public:
    TestSettingsIsolation()
    {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        llocr::SettingsStore::setSecretNamespace(kSecretNamespace);
    }

    TestSettingsIsolation(const TestSettingsIsolation &) = delete;
    TestSettingsIsolation &operator=(const TestSettingsIsolation &) = delete;

    // Removes the secrets the test wrote, so the keychain of the machine
    // keeps nothing the test left behind.
    void clearSecrets()
    {
        for (const QString &id : secretIds())
            llocr::SecretStore::remove(kSecretNamespace + id);
        llocr::SecretStore::forgetAllCached();
    }

    static QString secretNamespace() { return kSecretNamespace; }

private:
    static QStringList secretIds()
    {
        QStringList ids = {QStringLiteral("provider/apiKey"), QStringLiteral("hf/token")};
        for (const llocr::ConnectionRole role : llocr::connectionRoles)
            ids.append(QStringLiteral("provider/apiKey/") + llocr::connectionRoleName(role));
        return ids;
    }

    static const QString kSecretNamespace;

    QTemporaryDir m_dir;
};

inline const QString TestSettingsIsolation::kSecretNamespace = QStringLiteral("llocr-test/");
