#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "runtime/ServerCapabilities.h"
#include "runtime/ServerLaunchConfig.h"
#include "app/LaunchParametersModel.h"

using namespace llocr;

namespace {

QJsonObject objectFromJson(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

}  // namespace

class TestLaunchConfig : public QObject {
    Q_OBJECT

private slots:
    void argvCoreFirstThenProfileRows();
    void argvFlagRowsHaveNoValue();
    void argvSkipsReservedNames();
    void argvNumericFormatting();
    void argvVerbatimDashNames();
    void displayCommandEscapes();
    void cyrillicAndSpacePaths();

    // --- LaunchProfile parsing (file format) ---
    void parseFileProfiles();
    void parseParameterKinds();
    void parseErrors();

    // --- LaunchParametersModel (draft table) ---
    void modelEditKindSwitching();
    void modelAppendValidation();
    void modelRemoveRow();
};

// A fully capable, modern binary.
static ServerCapabilities modernCaps()
{
    ServerCapabilities caps;
    caps.versionText = QStringLiteral("b10594");
    caps.build = QStringLiteral("b10594");
    caps.supportsFlashAttn = true;
    caps.supportsFlashAttnValue = true;
    caps.supportsAlias = true;
    caps.supportsJinja = true;
    caps.supportsCacheTypeK = true;
    caps.supportsCacheTypeV = true;
    return caps;
}

static LaunchParameter makeParameter(const QString &name, LaunchValueKind kind,
                                     const QVariant &value, int order)
{
    LaunchParameter p;
    p.name = name;
    p.kind = kind;
    p.value = value;
    p.order = order;
    return p;
}

void TestLaunchConfig::argvCoreFirstThenProfileRows()
{
    ServerLaunchConfig cfg;
    cfg.program = QStringLiteral("llama-server");
    cfg.modelPath = QStringLiteral("/models/model.gguf");
    cfg.mmprojPath = QStringLiteral("/models/mmproj.gguf");
    cfg.modelAlias = QStringLiteral("llocr-local");
    cfg.host = QStringLiteral("127.0.0.1");
    cfg.port = 8080;
    cfg.parameters.append(makeParameter(
        QStringLiteral("ctx-size"), LaunchValueKind::Number, QVariant(16384.0), 1));
    cfg.parameters.append(makeParameter(
        QStringLiteral("no-warmup"), LaunchValueKind::Flag, QVariant(), 2));
    cfg.parameters.append(makeParameter(
        QStringLiteral("cache-type-k"), LaunchValueKind::Text,
        QVariant(QStringLiteral("f32")), 3));

    const QStringList args = cfg.toArguments(modernCaps());
    QCOMPARE(args,
             QStringList({QStringLiteral("--model"), QStringLiteral("/models/model.gguf"),
                          QStringLiteral("--mmproj"), QStringLiteral("/models/mmproj.gguf"),
                          QStringLiteral("--alias"), QStringLiteral("llocr-local"),
                          QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                          QStringLiteral("--port"), QStringLiteral("8080"),
                          // profile rows, in profile order
                          QStringLiteral("--ctx-size"), QStringLiteral("16384"),
                          QStringLiteral("--no-warmup"),
                          QStringLiteral("--cache-type-k"), QStringLiteral("f32")}));
}

void TestLaunchConfig::argvFlagRowsHaveNoValue()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/m.gguf");
    cfg.parameters.append(makeParameter(
        QStringLiteral("special"), LaunchValueKind::Flag, QVariant(), 1));
    cfg.parameters.append(makeParameter(
        QStringLiteral("flash-attn"), LaunchValueKind::Text,
        QVariant(QStringLiteral("off")), 2));

    const QStringList args = cfg.toArguments(modernCaps());
    const int idx = args.indexOf(QStringLiteral("--special"));
    QVERIFY(idx >= 0);
    // The flag is a bare token: the next token is the following argument.
    QCOMPARE(args.at(idx + 1), QStringLiteral("--flash-attn"));
    QCOMPARE(args.at(idx + 2), QStringLiteral("off"));
}

void TestLaunchConfig::argvSkipsReservedNames()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/m.gguf");
    cfg.host = QStringLiteral("127.0.0.1");
    cfg.port = 9000;
    // A profile row with a reserved name must not duplicate the core flags.
    cfg.parameters.append(makeParameter(
        QStringLiteral("port"), LaunchValueKind::Number, QVariant(1.0), 1));
    cfg.parameters.append(makeParameter(
        QStringLiteral("model"), LaunchValueKind::Text,
        QVariant(QStringLiteral("evil.gguf")), 2));
    cfg.parameters.append(makeParameter(
        QStringLiteral("host"), LaunchValueKind::Text,
        QVariant(QStringLiteral("0.0.0.0")), 3));

    const QStringList args = cfg.toArguments(modernCaps());
    QCOMPARE(args.count(QStringLiteral("--port")), 1);
    QCOMPARE(args.count(QStringLiteral("--model")), 1);
    QCOMPARE(args.count(QStringLiteral("--host")), 1);
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--port")) + 1),
             QStringLiteral("9000"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--host")) + 1),
             QStringLiteral("127.0.0.1"));
    QCOMPARE(args.at(args.indexOf(QStringLiteral("--model")) + 1),
             QStringLiteral("/m.gguf"));
}

void TestLaunchConfig::argvNumericFormatting()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/m.gguf");
    cfg.parameters.append(makeParameter(
        QStringLiteral("ctx-size"), LaunchValueKind::Number, QVariant(16384.0), 1));
    cfg.parameters.append(makeParameter(
        QStringLiteral("cache-reuse"), LaunchValueKind::Number, QVariant(0.0), 2));

    const QStringList args = cfg.toArguments(modernCaps());
    QVERIFY(args.contains(QStringLiteral("16384")));
    QVERIFY(args.contains(QStringLiteral("0")));
}

void TestLaunchConfig::argvVerbatimDashNames()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/m.gguf");
    cfg.parameters.append(makeParameter(
        QStringLiteral("-ctk"), LaunchValueKind::Text,
        QVariant(QStringLiteral("q8_0")), 1));

    const QStringList args = cfg.toArguments(modernCaps());
    QVERIFY(args.contains(QStringLiteral("-ctk")));
    QVERIFY(args.contains(QStringLiteral("q8_0")));
}

void TestLaunchConfig::displayCommandEscapes()
{
    ServerLaunchConfig cfg;
    cfg.program = QStringLiteral("llama server");
    cfg.modelPath = QStringLiteral("/my models/модель.gguf");

    const QString cmd = cfg.toDisplayCommand(modernCaps());
    QVERIFY(cmd.startsWith(QStringLiteral("\"llama server\"")));
    QVERIFY(cmd.contains(QStringLiteral("\"/my models/модель.gguf\"")));
}

void TestLaunchConfig::cyrillicAndSpacePaths()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/path with space/файл-\"q\".gguf");
    const QString cmd = cfg.toDisplayCommand(modernCaps());
    QVERIFY(cmd.contains(QStringLiteral("\"/path with space/файл-\\\"q\\\".gguf\"")));
}

void TestLaunchConfig::parseFileProfiles()
{
    const QByteArray json = R"({
        "schemaVersion": 1,
        "profiles": [
            { "id": "macos-metal", "name": "macOS Metal", "os": "macos",
              "backend": "metal",
              "parameters": [
                  { "order": 1, "name": "n-gpu-layers", "value": 99 },
                  { "order": 2, "name": "special" },
                  { "order": 3, "name": "cache-type-k", "value": "f32",
                    "description": "KV cache K type" }
              ] },
            { "id": "cpu", "backend": "cpu", "parameters": [] }
        ]
    })";
    QString error;
    const QList<LaunchProfile> profiles =
        LaunchProfile::parseFile(objectFromJson(json), error);
    QVERIFY(error.isEmpty());
    QCOMPARE(profiles.size(), 2);
    QCOMPARE(profiles.at(0).id, QStringLiteral("macos-metal"));
    QCOMPARE(profiles.at(0).name, QStringLiteral("macOS Metal"));
    QCOMPARE(profiles.at(0).parameters.size(), 3);
    QCOMPARE(profiles.at(0).parameters.at(0).kind, LaunchValueKind::Number);
    QCOMPARE(profiles.at(0).parameters.at(1).kind, LaunchValueKind::Flag);
    QCOMPARE(profiles.at(0).parameters.at(2).description,
             QStringLiteral("KV cache K type"));
    // Missing display name falls back to the id.
    QCOMPARE(profiles.at(1).name, QStringLiteral("cpu"));

    // Round-trip.
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    QJsonArray arr;
    for (const LaunchProfile &p : profiles)
        arr.append(p.toJson());
    root.insert(QStringLiteral("profiles"), arr);
    const QList<LaunchProfile> reparsed =
        LaunchProfile::parseFile(root, error);
    QVERIFY(error.isEmpty());
    QCOMPARE(reparsed.size(), 2);
    QCOMPARE(reparsed.at(0).parameters.size(), 3);
    QCOMPARE(reparsed.at(0).parameters.at(1).kind, LaunchValueKind::Flag);
}

void TestLaunchConfig::parseParameterKinds()
{
    // Missing order falls back to sequential positions; kinds map from the
    // JSON value types; a null value is a flag.
    const QByteArray json = R"({
        "profiles": [
            { "id": "p", "parameters": [
                { "name": "a", "value": 1 },
                { "name": "b", "value": null },
                { "name": "c" },
                { "name": "d", "value": "x" }
            ] }
        ]
    })";
    QString error;
    const QList<LaunchProfile> profiles =
        LaunchProfile::parseFile(objectFromJson(json), error);
    QVERIFY(error.isEmpty());
    const QList<LaunchParameter> &params = profiles.first().parameters;
    QCOMPARE(params.size(), 4);
    QCOMPARE(params.at(0).order, 1);
    QCOMPARE(params.at(1).order, 2);
    QCOMPARE(params.at(2).order, 3);
    QCOMPARE(params.at(3).order, 4);
    QCOMPARE(params.at(0).kind, LaunchValueKind::Number);
    QCOMPARE(params.at(1).kind, LaunchValueKind::Flag);
    QCOMPARE(params.at(2).kind, LaunchValueKind::Flag);
    QCOMPARE(params.at(3).kind, LaunchValueKind::Text);
}

void TestLaunchConfig::parseErrors()
{
    QString error;

    // Duplicate profile ids are ambiguous.
    const QByteArray dupProfile = R"({
        "profiles": [ { "id": "x" }, { "id": "x" } ]
    })";
    LaunchProfile::parseFile(objectFromJson(dupProfile), error);
    QVERIFY(!error.isEmpty());

    // Duplicate parameter names within a profile are ambiguous.
    const QByteArray dupParam = R"({
        "profiles": [ { "id": "x", "parameters": [
            { "name": "a", "value": 1 }, { "name": "a", "value": 2 } ] } ]
    })";
    LaunchProfile::parseFile(objectFromJson(dupParam), error);
    QVERIFY(!error.isEmpty());

    // bool / array / object values make no sense on a command line.
    const QByteArray badValue = R"({
        "profiles": [ { "id": "x", "parameters": [
            { "name": "a", "value": true } ] } ]
    })";
    LaunchProfile::parseFile(objectFromJson(badValue), error);
    QVERIFY(!error.isEmpty());
}

void TestLaunchConfig::modelEditKindSwitching()
{
    LaunchParametersModel model;
    QList<LaunchParameter> rows;
    rows.append(makeParameter(QStringLiteral("num"), LaunchValueKind::Number,
                              QVariant(99.0), 1));
    rows.append(makeParameter(QStringLiteral("flag"), LaunchValueKind::Flag,
                              QVariant(), 2));
    rows.append(makeParameter(QStringLiteral("text"), LaunchValueKind::Text,
                              QVariant(QStringLiteral("f32")), 3));
    model.resetFrom(rows);

    // Numeric text on a Number row.
    QVERIFY(model.setValue(0, "16384"));
    QCOMPARE(model.data(model.index(0), LaunchParametersModel::ValueTextRole),
             QStringLiteral("16384"));
    // Non-numeric text is rejected on a Number row.
    QVERIFY(!model.setValue(0, "abc"));
    QCOMPARE(model.data(model.index(0), LaunchParametersModel::ValueTextRole),
             QStringLiteral("16384"));

    // Emptying a value turns the row into a flag.
    QVERIFY(model.setValue(2, ""));
    QCOMPARE(model.data(model.index(2), LaunchParametersModel::KindRole),
             int(LaunchValueKind::Flag));
    QCOMPARE(model.data(model.index(2), LaunchParametersModel::ValueTextRole),
             QStringLiteral(""));

    // Filling a flag row infers Number or Text.
    QVERIFY(model.setValue(1, "8192"));
    QCOMPARE(model.data(model.index(1), LaunchParametersModel::KindRole),
             int(LaunchValueKind::Number));
    QVERIFY(model.setValue(2, "none"));
    QCOMPARE(model.data(model.index(2), LaunchParametersModel::KindRole),
             int(LaunchValueKind::Text));

    // No-op edit still succeeds.
    QVERIFY(model.setValue(2, "none"));
}

void TestLaunchConfig::modelAppendValidation()
{
    LaunchParametersModel model;
    QList<LaunchParameter> rows;
    rows.append(makeParameter(QStringLiteral("ctx-size"), LaunchValueKind::Number,
                              QVariant(16384.0), 1));
    model.resetFrom(rows);

    // Empty / reserved / duplicate names are refused.
    QVERIFY(!model.appendRow(QStringLiteral(""), QString()));
    QVERIFY(!model.appendRow(QStringLiteral("model"), QString()));
    QVERIFY(!model.appendRow(QStringLiteral("mmproj"), QString()));
    QVERIFY(!model.appendRow(QStringLiteral("alias"), QString()));
    QVERIFY(!model.appendRow(QStringLiteral("host"), QString()));
    QVERIFY(!model.appendRow(QStringLiteral("port"), QString()));
    QVERIFY(!model.appendRow(QStringLiteral("ctx-size"), QString()));

    // A flag row and a text row are appended.
    QVERIFY(model.appendRow(QStringLiteral("--no-warmup"), QString()));
    QVERIFY(model.appendRow(QStringLiteral("flash-attn"), QStringLiteral("off")));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(1), LaunchParametersModel::NameRole),
             QStringLiteral("no-warmup"));  // leading dashes stripped
    QCOMPARE(model.data(model.index(1), LaunchParametersModel::KindRole),
             int(LaunchValueKind::Flag));
    QCOMPARE(model.data(model.index(2), LaunchParametersModel::KindRole),
             int(LaunchValueKind::Text));
}

void TestLaunchConfig::modelRemoveRow()
{
    LaunchParametersModel model;
    QList<LaunchParameter> rows;
    rows.append(makeParameter(QStringLiteral("a"), LaunchValueKind::Flag,
                              QVariant(), 1));
    rows.append(makeParameter(QStringLiteral("b"), LaunchValueKind::Flag,
                              QVariant(), 2));
    model.resetFrom(rows);

    model.removeRow(0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), LaunchParametersModel::NameRole),
             QStringLiteral("b"));
    model.removeRow(5);  // out of range: no-op
    QCOMPARE(model.rowCount(), 1);
}

QTEST_MAIN(TestLaunchConfig)
#include "test_launch_config.moc"
