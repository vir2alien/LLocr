#include "app/VerificationPromptStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include "app/SettingsStore.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

namespace {
constexpr int kSchemaVersion = 1;
constexpr const char kBuiltInPath[] = ":/profiles/verifyPrompts.json";
constexpr const char kUserFileName[] = "verifyPrompts.json";
}  // namespace

// ---------------------------------------------------------------- model ----

VerificationBlocksModel::VerificationBlocksModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int VerificationBlocksModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_blocks.size();
}

QVariant VerificationBlocksModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.size())
        return QVariant();

    const VerificationBlock &block = m_blocks.at(index.row());
    switch (role) {
    case TypeRole:    return block.type;
    case NameRole:    return block.name;
    case GroupRole:   return block.group;
    case EnabledRole: return block.enabled;
    case PromptRole:  return block.prompt;
    default:          return QVariant();
    }
}

QHash<int, QByteArray> VerificationBlocksModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        { TypeRole,    "type" },
        { NameRole,    "name" },
        { GroupRole,   "group" },
        { EnabledRole, "enabled" },
        { PromptRole,  "prompt" },
    };
    return roles;
}

void VerificationBlocksModel::resetFrom(const QList<VerificationBlock> &blocks)
{
    beginResetModel();
    m_blocks = blocks;
    endResetModel();
    ++m_revision;
    emit countsChanged();
}

void VerificationBlocksModel::setEnabled(int row, bool on)
{
    if (row < 0 || row >= m_blocks.size() || m_blocks[row].enabled == on)
        return;
    m_blocks[row].enabled = on;
    emit dataChanged(index(row), index(row), {EnabledRole});
    ++m_revision;
    emit countsChanged();
}

void VerificationBlocksModel::setAllEnabled(bool on)
{
    bool changed = false;
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks[i].enabled == on)
            continue;
        m_blocks[i].enabled = on;
        changed = true;
    }
    if (!changed || m_blocks.isEmpty())
        return;
    emit dataChanged(index(0), index(m_blocks.size() - 1), {EnabledRole});
    ++m_revision;
    emit countsChanged();
}

int VerificationBlocksModel::enabledCount() const
{
    int count = 0;
    for (const VerificationBlock &block : m_blocks) {
        if (block.enabled)
            ++count;
    }
    return count;
}

void VerificationBlocksModel::setPrompt(int row, const QString &text)
{
    if (row < 0 || row >= m_blocks.size() || m_blocks[row].prompt == text)
        return;
    m_blocks[row].prompt = text;
    emit dataChanged(index(row), index(row), {PromptRole});
    ++m_revision;
    emit countsChanged();
}

QString VerificationBlocksModel::typeAt(int row) const
{
    return row >= 0 && row < m_blocks.size() ? m_blocks.at(row).type : QString();
}

QString VerificationBlocksModel::nameAt(int row) const
{
    return row >= 0 && row < m_blocks.size() ? m_blocks.at(row).name : QString();
}

bool VerificationBlocksModel::enabledAt(int row) const
{
    return row >= 0 && row < m_blocks.size() && m_blocks.at(row).enabled;
}

QString VerificationBlocksModel::promptAt(int row) const
{
    return row >= 0 && row < m_blocks.size() ? m_blocks.at(row).prompt : QString();
}

// ------------------------------------------------------------- store -------

VerificationPromptStore::VerificationPromptStore(SettingsStore &settings,
                                                 QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(new VerificationBlocksModel(this))
{
    loadBuiltIn();
    loadUser();
    rebuildModel();
}

QString VerificationPromptStore::userPath() const
{
    return QDir(RuntimePaths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir())
                    .profilesDir())
        .filePath(QString::fromUtf8(kUserFileName));
}

void VerificationPromptStore::setSystemPrompt(const QString &text)
{
    if (m_systemPrompt == text)
        return;
    m_systemPrompt = text;
    emit systemPromptChanged();
}

QStringList VerificationPromptStore::blockTypes() const
{
    QStringList types;
    for (const VerificationBlock &block : m_model->blocks())
        types.append(block.type);
    return types;
}

int VerificationPromptStore::indexOfType(const QString &type) const
{
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks.at(i).type == type)
            return i;
    }
    return -1;
}

const VerificationBlock *VerificationPromptStore::findBlock(const QString &type) const
{
    const int i = indexOfType(type);
    return i >= 0 ? &m_blocks.at(i) : nullptr;
}

VerificationBlock *VerificationPromptStore::findBlock(const QString &type)
{
    const int i = indexOfType(type);
    return i >= 0 ? &m_blocks[i] : nullptr;
}

QString VerificationPromptStore::promptForType(const QString &type) const
{
    for (const VerificationBlock &block : m_model->blocks()) {
        if (block.type == type)
            return block.prompt;
    }
    return QString();
}

bool VerificationPromptStore::isTypeEnabled(const QString &type) const
{
    for (const VerificationBlock &block : m_model->blocks()) {
        if (block.type == type)
            return block.enabled;
    }
    return false;
}

void VerificationPromptStore::loadBuiltIn()
{
    m_originalPrompts.clear();

    QFile builtIn(QString::fromUtf8(kBuiltInPath));
    if (!builtIn.open(QIODevice::ReadOnly)) {
        qWarning("VerificationPromptStore: cannot open built-in prompts %s: %s",
                 qUtf8Printable(QString::fromUtf8(kBuiltInPath)),
                 qUtf8Printable(builtIn.errorString()));
        return;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("VerificationPromptStore: cannot parse built-in prompts: %s",
                 qUtf8Printable(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    m_systemPrompt = root.value(QStringLiteral("systemPrompt")).toString();
    m_originalSystemPrompt = m_systemPrompt;

    QList<VerificationBlock> blocks;
    const QJsonArray array = root.value(QStringLiteral("blocks")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        VerificationBlock block;
        block.type = obj.value(QStringLiteral("type")).toString();
        block.name = obj.value(QStringLiteral("name")).toString();
        block.group = obj.value(QStringLiteral("group")).toString(
            QStringLiteral("content"));
        block.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        block.prompt = obj.value(QStringLiteral("prompt")).toString();
        if (!block.type.isEmpty()) {
            m_originalPrompts.insert(block.type, block.prompt);
            blocks.append(block);
        }
    }
    m_blocks = blocks;
}

QString VerificationPromptStore::originalPromptAt(int row) const
{
    if (row < 0 || row >= m_blocks.size())
        return QString();
    return m_originalPrompts.value(m_blocks.at(row).type);
}

void VerificationPromptStore::loadUser()
{
    QFile userFile(userPath());
    if (!userFile.exists())
        return;

    QJsonParseError parseError{};
    if (!userFile.open(QIODevice::ReadOnly)) {
        qWarning("VerificationPromptStore: cannot open user prompts %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(userFile.errorString()));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(userFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("VerificationPromptStore: cannot parse user prompts %s: %s "
                 "(falling back to the built-in prompts)",
                 qUtf8Printable(userPath()), qUtf8Printable(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("schemaVersion"))) {
        const int version = root.value(QStringLiteral("schemaVersion")).toInt(-1);
        if (version > kSchemaVersion) {
            qWarning("VerificationPromptStore: user prompts %s use unsupported "
                     "schema version %d (supported: %d); applying what can be parsed",
                     qUtf8Printable(userPath()), version, kSchemaVersion);
        }
    }
    if (root.contains(QStringLiteral("systemPrompt")))
        m_systemPrompt = root.value(QStringLiteral("systemPrompt")).toString();

    const QJsonArray array = root.value(QStringLiteral("blocks")).toArray();
    for (const QJsonValue &value : array) {
        const QJsonObject obj = value.toObject();
        const QString type = obj.value(QStringLiteral("type")).toString();
        VerificationBlock *block = findBlock(type);
        if (!block) {
            if (!type.isEmpty()) {
                qWarning("VerificationPromptStore: user prompts reference unknown "
                         "block type '%s'; entry ignored",
                         qUtf8Printable(type));
            }
            continue;
        }
        if (obj.contains(QStringLiteral("enabled")))
            block->enabled = obj.value(QStringLiteral("enabled")).toBool(true);
        if (obj.contains(QStringLiteral("prompt")))
            block->prompt = obj.value(QStringLiteral("prompt")).toString();
    }
}

void VerificationPromptStore::rebuildModel()
{
    m_model->resetFrom(m_blocks);
    emit modelChanged();
}

void VerificationPromptStore::loadValues()
{
    loadBuiltIn();
    loadUser();
    rebuildModel();
    emit systemPromptChanged();
}

void VerificationPromptStore::save()
{
    const QString builtInPath = QString::fromUtf8(kBuiltInPath);
    QList<VerificationBlock> builtIn;
    QString builtInSystem;
    {
        QFile builtInFile(builtInPath);
        if (builtInFile.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(builtInFile.readAll());
            if (doc.isObject()) {
                const QJsonObject root = doc.object();
                builtInSystem = root.value(QStringLiteral("systemPrompt")).toString();
                const QJsonArray array = root.value(QStringLiteral("blocks")).toArray();
                for (const QJsonValue &value : array) {
                    const QJsonObject obj = value.toObject();
                    VerificationBlock block;
                    block.type = obj.value(QStringLiteral("type")).toString();
                    block.name = obj.value(QStringLiteral("name")).toString();
                    block.enabled = obj.value(QStringLiteral("enabled")).toBool(true);
                    block.prompt = obj.value(QStringLiteral("prompt")).toString();
                    if (!block.type.isEmpty())
                        builtIn.append(block);
                }
            }
        }
    }

    const bool systemChanged = m_systemPrompt != builtInSystem;
    QJsonArray changedBlocks;
    for (const VerificationBlock &block : m_model->blocks()) {
        bool differs = true;
        for (const VerificationBlock &base : std::as_const(builtIn)) {
            if (base.type != block.type)
                continue;
            differs = base.enabled != block.enabled || base.prompt != block.prompt;
            break;
        }
        if (differs) {
            QJsonObject obj{{QStringLiteral("type"), block.type}};
            obj.insert(QStringLiteral("enabled"), block.enabled);
            obj.insert(QStringLiteral("prompt"), block.prompt);
            obj.insert(QStringLiteral("name"), block.name);
            changedBlocks.append(obj);
        }
    }

    if (!systemChanged && changedBlocks.isEmpty()) {
        QFile file(userPath());
        if (file.exists() && !file.remove())
            qWarning("VerificationPromptStore: cannot remove user prompts %s: %s",
                     qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }

    QDir().mkpath(QFileInfo(userPath()).absolutePath());
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    if (systemChanged)
        root.insert(QStringLiteral("systemPrompt"), m_systemPrompt);
    if (!changedBlocks.isEmpty())
        root.insert(QStringLiteral("blocks"), changedBlocks);

    QSaveFile file(userPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("VerificationPromptStore: cannot write user prompts %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("VerificationPromptStore: cannot commit user prompts %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
}

void VerificationPromptStore::resetToDefaults()
{
    QFile file(userPath());
    if (file.exists() && !file.remove())
        qWarning("VerificationPromptStore: cannot remove user prompts %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
    loadValues();
}

}  // namespace llocr