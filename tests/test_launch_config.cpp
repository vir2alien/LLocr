#include <QtTest>

#include "runtime/ServerCapabilities.h"
#include "runtime/ServerLaunchConfig.h"

using namespace llocr;

class TestLaunchConfig : public QObject {
    Q_OBJECT

private slots:
    void argvBasics();
    void argvSkipsDefaults();
    void argvRespectsCapabilities();
    void flashAttnBareVsValue();
    void parseExtraArgsQuotesAndSpaces();
    void unclosedQuoteKeepsRest();
    void displayCommandEscapes();
    void cyrillicAndSpacePaths();
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

void TestLaunchConfig::argvBasics()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/models/model.gguf");
    cfg.mmprojPath = QStringLiteral("/models/mmproj.gguf");
    cfg.modelAlias = QStringLiteral("llocr-local");
    cfg.host = QStringLiteral("127.0.0.1");
    cfg.port = 8080;
    cfg.ctxSize = 8192;
    cfg.gpuLayers = 33;
    cfg.threads = 8;
    cfg.batchSize = 512;
    cfg.parallel = 2;
    cfg.flashAttn = QStringLiteral("on");
    cfg.noMmap = true;
    cfg.jinja = true;
    cfg.cacheTypeK = QStringLiteral("q8_0");
    cfg.cacheTypeV = QStringLiteral("q8_0");

    const QStringList args = cfg.toArguments(modernCaps());
    QCOMPARE(args,
             QStringList({QStringLiteral("--model"), QStringLiteral("/models/model.gguf"),
                          QStringLiteral("--mmproj"), QStringLiteral("/models/mmproj.gguf"),
                          QStringLiteral("--alias"), QStringLiteral("llocr-local"),
                          QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                          QStringLiteral("--port"), QStringLiteral("8080"),
                          QStringLiteral("--ctx-size"), QStringLiteral("8192"),
                          QStringLiteral("--n-gpu-layers"), QStringLiteral("33"),
                          QStringLiteral("--threads"), QStringLiteral("8"),
                          QStringLiteral("--batch-size"), QStringLiteral("512"),
                          QStringLiteral("--parallel"), QStringLiteral("2"),
                          QStringLiteral("--flash-attn"), QStringLiteral("on"),
                          QStringLiteral("-ctk"), QStringLiteral("q8_0"),
                          QStringLiteral("-ctv"), QStringLiteral("q8_0"),
                          QStringLiteral("--no-mmap"), QStringLiteral("--jinja")}));
}

void TestLaunchConfig::argvSkipsDefaults()
{
    ServerLaunchConfig cfg;  // all defaults
    cfg.modelPath = QStringLiteral("/m.gguf");

    // Defaults that mean "do not pass": port 0 (auto-pick), gpuLayers -1,
    // threads 0, batchSize 0. Parallel defaults to 1 and is always passed.
    const QStringList args = cfg.toArguments(modernCaps());
    QVERIFY(!args.contains(QStringLiteral("--port")));
    QVERIFY(!args.contains(QStringLiteral("--n-gpu-layers")));
    QVERIFY(!args.contains(QStringLiteral("--threads")));
    QVERIFY(!args.contains(QStringLiteral("--batch-size")));
    QVERIFY(args.contains(QStringLiteral("--parallel")));
    QVERIFY(args.contains(QStringLiteral("--ctx-size")));  // default 8192 > 0
}

void TestLaunchConfig::argvRespectsCapabilities()
{
    // A binary that predates --alias / --jinja / -ctk / -ctv.
    ServerCapabilities caps;
    caps.build = QStringLiteral("b4000");
    caps.supportsFlashAttn = true;
    caps.supportsFlashAttnValue = false;
    caps.supportsAlias = false;

    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/m.gguf");
    cfg.modelAlias = QStringLiteral("llocr-local");
    cfg.jinja = true;
    cfg.cacheTypeK = QStringLiteral("q8_0");
    cfg.flashAttn = QStringLiteral("on");

    const QStringList args = cfg.toArguments(caps);
    QVERIFY(!args.contains(QStringLiteral("--alias")));
    QVERIFY(!args.contains(QStringLiteral("--jinja")));
    QVERIFY(!args.contains(QStringLiteral("-ctk")));
    // flash-attn on a bare-boolean build becomes a single bare flag.
    QVERIFY(args.contains(QStringLiteral("--flash-attn")));
    QCOMPARE(args.count(QStringLiteral("--flash-attn")), 1);
}

void TestLaunchConfig::flashAttnBareVsValue()
{
    ServerCapabilities modern = modernCaps();
    ServerCapabilities bare;
    bare.supportsFlashAttn = true;
    bare.supportsFlashAttnValue = false;

    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/m.gguf");
    cfg.flashAttn = QStringLiteral("on");

    // Modern: "--flash-attn" then "on".
    {
        const QStringList args = cfg.toArguments(modern);
        const int idx = args.indexOf(QStringLiteral("--flash-attn"));
        QVERIFY(idx >= 0);
        QCOMPARE(args.at(idx + 1), QStringLiteral("on"));
    }

    // Bare-boolean: only the flag, no following value.
    {
        const QStringList args = cfg.toArguments(bare);
        const int idx = args.indexOf(QStringLiteral("--flash-attn"));
        QVERIFY(idx >= 0);
        QVERIFY(idx + 1 >= args.size() || args.at(idx + 1).startsWith(QStringLiteral("--")));
    }

    // "off" on a bare-boolean build must NOT emit the flag at all.
    {
        cfg.flashAttn = QStringLiteral("off");
        const QStringList args = cfg.toArguments(bare);
        QVERIFY(!args.contains(QStringLiteral("--flash-attn")));
    }
}

void TestLaunchConfig::parseExtraArgsQuotesAndSpaces()
{
    const QString input =
        QStringLiteral("--rope-scaling yarn --yarn-factor \"2.5\" --spaced 'a b c' -x");
    const QStringList tokens = ServerLaunchConfig::parseExtraArgs(input);
    QCOMPARE(tokens,
             QStringList({QStringLiteral("--rope-scaling"), QStringLiteral("yarn"),
                          QStringLiteral("--yarn-factor"), QStringLiteral("2.5"),
                          QStringLiteral("--spaced"), QStringLiteral("a b c"),
                          QStringLiteral("-x")}));
}

void TestLaunchConfig::unclosedQuoteKeepsRest()
{
    const QString input = QStringLiteral("--foo \"unterminated rest");
    const QStringList tokens = ServerLaunchConfig::parseExtraArgs(input);
    QCOMPARE(tokens, QStringList({QStringLiteral("--foo"), QStringLiteral("unterminated rest")}));
}

void TestLaunchConfig::displayCommandEscapes()
{
    ServerLaunchConfig cfg;
    cfg.program = QStringLiteral("/opt/llama.cpp/llama-server");
    cfg.modelPath = QStringLiteral("/Users/my user/Модели/модель.gguf");
    cfg.parallel = 1;

    const QString cmd = cfg.toDisplayCommand(modernCaps());
    QVERIFY(cmd.contains(QStringLiteral("\"/Users/my user/Модели/модель.gguf\"")));
    // The program itself needs no quotes (no spaces), and the model value is
    // wrapped. Check overall shape: program first.
    QVERIFY(cmd.startsWith(QStringLiteral("/opt/llama.cpp/llama-server")));
}

void TestLaunchConfig::cyrillicAndSpacePaths()
{
    ServerLaunchConfig cfg;
    cfg.modelPath = QStringLiteral("/Данные/Мой файл.gguf");
    cfg.extraArgs = {QStringLiteral("--whatever"), QStringLiteral("значение")};

    const QStringList args = cfg.toArguments(modernCaps());
    QCOMPARE(args.at(1), QStringLiteral("/Данные/Мой файл.gguf"));  // --model value
    QVERIFY(args.contains(QStringLiteral("значение")));

    const QString display = cfg.toDisplayCommand(modernCaps());
    QVERIFY(display.contains(QStringLiteral("\"/Данные/Мой файл.gguf\"")));
    QVERIFY(!display.contains(QStringLiteral("\n")));
}

QTEST_MAIN(TestLaunchConfig)
#include "test_launch_config.moc"