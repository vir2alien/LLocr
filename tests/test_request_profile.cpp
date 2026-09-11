#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>

#include "app/RequestProfileStore.h"
#include "app/SettingsStore.h"
#include "testsettings.h"

using namespace llocr;

namespace {

// Built-in-style defaults: two parameters to exercise merge ordering.
constexpr const char *kDefaultsJson = R"({
    "schemaVersion": 1,
    "parameters": [
        { "order": 1, "name": "alpha", "value": 0.8 },
        { "order": 2, "name": "beta",  "value": 35 }
    ]
})";

// Writes a built-in-style profile to a temp file and returns its path (the
// caller checks the result with QVERIFY — helpers cannot use QVERIFY because
// it expands to a bare `return;`).
QString writeProfile(const QTemporaryDir &dir, const QString &name,
                     const QByteArray &json)
{
    const QString path = QDir(dir.path()).filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(json);
    return path;
}

QJsonObject objectFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

const RequestParameter *findParameter(const RequestProfile &profile,
                                      const QString &name)
{
    for (const RequestParameter &p : profile.parameters)
        if (p.name == name)
            return &p;
    return nullptr;
}

RequestProfile parseDefaults()
{
    QString error;
    const RequestProfile profile =
        RequestProfile::fromJson(objectFromJson(kDefaultsJson), error);
    Q_ASSERT(error.isEmpty());
    return profile;
}

}  // namespace

class TestRequestProfile : public QObject {
    Q_OBJECT

private:
    // Must precede any SettingsStore created by the tests (see the header).
    TestSettingsIsolation m_settingsIsolation;

private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("llocr_test"));
        QCoreApplication::setApplicationName(QStringLiteral("test_request_profile"));
    }

    // --- RequestProfile (parse / serialize / merge / values) ---

    void parseRoundTrip()
    {
        QString error;
        const RequestProfile profile =
            RequestProfile::fromJson(objectFromJson(kDefaultsJson), error);
        QVERIFY(error.isEmpty());
        QCOMPARE(profile.parameters.size(), 2);
        QCOMPARE(profile.parameters.at(0).name, QStringLiteral("alpha"));
        QCOMPARE(profile.parameters.at(0).order, 1);
        QCOMPARE(profile.parameters.at(0).value.toDouble(), 0.8);
        QCOMPARE(profile.parameters.at(1).name, QStringLiteral("beta"));
        QCOMPARE(profile.parameters.at(1).kind, RequestValueKind::Number);

        const QJsonObject back = profile.toJson();
        QString error2;
        const RequestProfile reparsed = RequestProfile::fromJson(back, error2);
        QVERIFY(error2.isEmpty());
        QVERIFY(profile == reparsed);
    }

    void parseErrors()
    {
        QString error;

        RequestProfile::fromJson(objectFromJson("{}"), error);
        QVERIFY(!error.isEmpty());

        // Duplicate names make overrides ambiguous.
        const QByteArray dup = R"({
            "parameters": [
                { "order": 1, "name": "a", "value": 1 },
                { "order": 2, "name": "a", "value": 2 }
            ]
        })";
        RequestProfile::fromJson(objectFromJson(dup), error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("a")));

        // Unsupported values (object / mixed array) are rejected.
        const QByteArray badValue = R"({
            "parameters": [ { "order": 1, "name": "a", "value": { "x": 1 } } ]
        })";
        RequestProfile::fromJson(objectFromJson(badValue), error);
        QVERIFY(!error.isEmpty());

        const QByteArray mixedArray = R"({
            "parameters": [ { "order": 1, "name": "a", "value": [1, "x"] } ]
        })";
        RequestProfile::fromJson(objectFromJson(mixedArray), error);
        QVERIFY(!error.isEmpty());
    }

    void valueKindsPreserved()
    {
        const QByteArray json = R"({
            "parameters": [
                { "order": 1, "name": "num",   "value": 8192 },
                { "order": 2, "name": "frac",  "value": 0.8 },
                { "order": 3, "name": "flag",  "value": false },
                { "order": 4, "name": "breakers", "value": [] },
                { "order": 5, "name": "words", "value": ["a", "b"] }
            ]
        })";
        QString error;
        const RequestProfile profile =
            RequestProfile::fromJson(objectFromJson(json), error);
        QVERIFY(error.isEmpty());

        const RequestParameter *num = findParameter(profile, "num");
        QVERIFY(num);
        QCOMPARE(num->kind, RequestValueKind::Number);
        QCOMPARE(num->value.typeId(), QMetaType::Double);

        const RequestParameter *flag = findParameter(profile, "flag");
        QVERIFY(flag);
        QCOMPARE(flag->kind, RequestValueKind::Boolean);
        QCOMPARE(flag->value.toBool(), false);

        const RequestParameter *breakers = findParameter(profile, "breakers");
        QVERIFY(breakers);
        QCOMPARE(breakers->kind, RequestValueKind::StringList);
        QCOMPARE(breakers->value.toStringList().size(), 0);

        const RequestParameter *words = findParameter(profile, "words");
        QVERIFY(words);
        QCOMPARE(words->value.toStringList(), (QStringList{"a", "b"}));

        // Serialization keeps the JSON kinds: booleans stay booleans, arrays
        // stay arrays, numbers stay numbers.
        const QJsonObject out = profile.toJson();
        const QJsonArray params = out.value("parameters").toArray();
        QCOMPARE(params.at(2).toObject().value("value").type(), QJsonValue::Bool);
        QCOMPARE(params.at(3).toObject().value("value").type(), QJsonValue::Array);
        QCOMPARE(params.at(3).toObject().value("value").toArray().size(), 0);
        QCOMPARE(params.at(0).toObject().value("value").type(), QJsonValue::Double);
    }

    void mergeOverridesByNameAndKeepsNewDefaults()
    {
        QString error;
        const RequestProfile defaults =
            RequestProfile::fromJson(objectFromJson(kDefaultsJson), error);
        QVERIFY(error.isEmpty());

        // User overrides alpha, and still carries a stale parameter "old"
        // (absent from the current defaults).
        const QByteArray userJson = R"({
            "parameters": [
                { "order": 1, "name": "alpha", "value": 0.5 },
                { "order": 2, "name": "beta",  "value": 35 },
                { "order": 9, "name": "old",   "value": "x" }
            ]
        })";
        const RequestProfile user =
            RequestProfile::fromJson(objectFromJson(userJson), error);
        QVERIFY(error.isEmpty());

        const RequestProfile merged = RequestProfile::merge(defaults, user);
        QCOMPARE(merged.parameters.size(), 3);
        // Built-in position wins; user value wins.
        QCOMPARE(merged.parameters.at(0).name, QStringLiteral("alpha"));
        QCOMPARE(merged.parameters.at(0).order, 1);
        QCOMPARE(merged.parameters.at(0).value.toDouble(), 0.5);
        QCOMPARE(merged.parameters.at(1).name, QStringLiteral("beta"));
        QCOMPARE(merged.parameters.at(1).value.toDouble(), 35.0);
        // User-only parameters are appended after the built-in ones.
        QCOMPARE(merged.parameters.at(2).name, QStringLiteral("old"));

        // A default parameter missing from the user file stays (new built-in
        // parameters appear automatically).
        const QByteArray partialJson = R"({
            "parameters": [
                { "order": 1, "name": "alpha", "value": 0.5 }
            ]
        })";
        const RequestProfile partial =
            RequestProfile::fromJson(objectFromJson(partialJson), error);
        QVERIFY(error.isEmpty());
        const RequestProfile merged2 = RequestProfile::merge(defaults, partial);
        QCOMPARE(merged2.parameters.size(), 2);
        QCOMPARE(merged2.parameters.at(1).name, QStringLiteral("beta"));
        QCOMPARE(merged2.parameters.at(1).value.toDouble(), 35.0);
    }

    void textParsingIsKindStrict()
    {
        QVariant out;

        // Numbers: strict finite-number text.
        QVERIFY(RequestProfile::textToValue("0.5", RequestValueKind::Number, out));
        QCOMPARE(out.toDouble(), 0.5);
        QVERIFY(RequestProfile::textToValue("8192", RequestValueKind::Number, out));
        QCOMPARE(out.toDouble(), 8192.0);
        QVERIFY(!RequestProfile::textToValue("abc", RequestValueKind::Number, out));
        QVERIFY(!RequestProfile::textToValue("", RequestValueKind::Number, out));

        // Booleans: only true/false, never "1"/"0"/"yes".
        QVERIFY(RequestProfile::textToValue("True", RequestValueKind::Boolean, out));
        QCOMPARE(out.toBool(), true);
        QVERIFY(RequestProfile::textToValue("false", RequestValueKind::Boolean, out));
        QVERIFY(!RequestProfile::textToValue("1", RequestValueKind::Boolean, out));
        QVERIFY(!RequestProfile::textToValue("yes", RequestValueKind::Boolean, out));

        // String lists: comma-separated, trimmed, empty segments dropped.
        QVERIFY(RequestProfile::textToValue("a, b,,c", RequestValueKind::StringList, out));
        QCOMPARE(out.toStringList(), (QStringList{"a", "b", "c"}));
        QVERIFY(RequestProfile::textToValue("", RequestValueKind::StringList, out));
        QCOMPARE(out.toStringList().size(), 0);
    }

    // --- RequestParametersModel (draft table) ---

    void modelEditValidation()
    {
        RequestParametersModel model;
        QString error;
        const RequestProfile parsed =
            RequestProfile::fromJson(objectFromJson(kDefaultsJson), error);
        QVERIFY(error.isEmpty());
        model.resetFrom(parsed.parameters);
        QCOMPARE(model.rowCount(), 2);

        // Valid numeric edit updates the row.
        QVERIFY(model.setValue(0, "0.5"));
        QCOMPARE(model.data(model.index(0), RequestParametersModel::ValueTextRole),
                 QStringLiteral("0.5"));
        QVERIFY(model.parameters().at(0).value.toDouble() == 0.5);

        // Invalid edit is rejected and leaves the row untouched.
        QVERIFY(!model.setValue(0, "abc"));
        QCOMPARE(model.data(model.index(0), RequestParametersModel::ValueTextRole),
                 QStringLiteral("0.5"));

        // Out-of-range rows are refused.
        QVERIFY(!model.setValue(5, "1"));
    }

    // --- RequestProfileStore (persisted state, draft/save lifecycle) ---

    void storeLifecycle()
    {
        QTemporaryDir dir;
        const QString defaultsPath =
            writeProfile(dir, "defaults.json", kDefaultsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));

        RequestProfileStore store(settings, defaultsPath);

        // Nothing changed yet: no user profile, active == defaults.
        QVERIFY(!store.hasUserProfile());
        QCOMPARE(store.activeProfile().parameters.size(), 2);
        QVERIFY(store.activeProfile() == parseDefaults());

        // Draft edits do not touch the persisted profile.
        QVERIFY(store.setDraftValue(0, "0.1"));
        QCOMPARE(findParameter(store.activeProfile(), "alpha")->value.toDouble(),
                 0.8);

        // Save commits the draft and creates the user profile.
        store.saveDraft();
        QVERIFY(store.hasUserProfile());
        QCOMPARE(findParameter(store.activeProfile(), "alpha")->value.toDouble(),
                 0.1);

        // A fresh store reads the saved values back.
        RequestProfileStore store2(settings, defaultsPath);
        QCOMPARE(findParameter(store2.activeProfile(), "alpha")->value.toDouble(),
                 0.1);

        // Saving defaults removes the user profile again.
        store2.loadDefaultDraft();
        QCOMPARE(findParameter(store2.activeProfile(), "alpha")->value.toDouble(),
                 0.1);  // active is untouched by draft edits
        store2.saveDraft();
        QVERIFY(!store2.hasUserProfile());
        QCOMPARE(findParameter(store2.activeProfile(), "alpha")->value.toDouble(),
                 0.8);
        QVERIFY(store2.activeProfile() == parseDefaults());
    }

    void storeCorruptUserFallsBackToDefaults()
    {
        QTemporaryDir dir;
        const QString defaultsPath =
            writeProfile(dir, "defaults.json", kDefaultsJson);

        SettingsStore settings;
        settings.setRuntimeRootDir(dir.path());
        settings.setRuntimeModelsDir(QDir(dir.path()).filePath("models"));

        const QString userPath = QDir(dir.path())
                                     .filePath(QStringLiteral("profiles/request.json"));
        QVERIFY(QDir().mkpath(QFileInfo(userPath).absolutePath()));
        QFile broken(userPath);
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("{ not valid json");
        broken.close();

        RequestProfileStore store(settings, defaultsPath);
        // Corrupt user profile: defaults win instead of failing.
        QVERIFY(store.activeProfile() == parseDefaults());
    }
};

QTEST_MAIN(TestRequestProfile)
#include "test_request_profile.moc"
