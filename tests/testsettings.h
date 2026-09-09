#pragma once

#include <QSettings>
#include <QTemporaryDir>

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
// Declare it as the FIRST data member of the test class: it must be
// constructed before any SettingsStore and outlive every test function
// (QTEST_MAIN creates the test object once, before running all slots).
class TestSettingsIsolation
{
public:
    TestSettingsIsolation()
    {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           m_dir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    TestSettingsIsolation(const TestSettingsIsolation &) = delete;
    TestSettingsIsolation &operator=(const TestSettingsIsolation &) = delete;

private:
    QTemporaryDir m_dir;
};
