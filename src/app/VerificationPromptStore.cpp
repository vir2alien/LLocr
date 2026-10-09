#include "app/VerificationPromptStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "app/BlockGroupFilterModel.h"
#include "config/ProfileStorage.h"
#include "config/RuntimePaths.h"
#include "config/SettingsStore.h"

namespace llocr {

namespace {

constexpr int kSchemaVersion = 1;
constexpr const char kUserFileName[] = "verifyPrompts.json";

QList<VerificationBlock> builtInBlocks()
{
    return {
        {QStringLiteral("text"), QStringLiteral("Text"), QStringLiteral("content"), true},
        {QStringLiteral("title"), QStringLiteral("Title"), QStringLiteral("content"), true},
        {QStringLiteral("table"), QStringLiteral("Table"), QStringLiteral("content"), true},
        {QStringLiteral("equation"), QStringLiteral("Equation"), QStringLiteral("content"), true},
        {QStringLiteral("formula"), QStringLiteral("Formula"), QStringLiteral("content"), true},
        {QStringLiteral("list"), QStringLiteral("List"), QStringLiteral("content"), true},
        {QStringLiteral("code"), QStringLiteral("Code"), QStringLiteral("content"), true},
        {QStringLiteral("abstract"), QStringLiteral("Abstract"), QStringLiteral("content"), true},
        {QStringLiteral("image_caption"), QStringLiteral("Image caption"), QStringLiteral("captions"), true},
        {QStringLiteral("table_caption"), QStringLiteral("Table caption"), QStringLiteral("captions"), true},
        {QStringLiteral("figure_footnote"), QStringLiteral("Figure footnote"), QStringLiteral("captions"), true},
        {QStringLiteral("table_footnote"), QStringLiteral("Table footnote"), QStringLiteral("captions"), true},
        {QStringLiteral("ref_text"), QStringLiteral("Reference text"), QStringLiteral("captions"), true},
        {QStringLiteral("reference"), QStringLiteral("Reference"), QStringLiteral("captions"), true},
        {QStringLiteral("header"), QStringLiteral("Header"), QStringLiteral("service"), false},
        {QStringLiteral("footer"), QStringLiteral("Footer"), QStringLiteral("service"), false},
        {QStringLiteral("page_number"), QStringLiteral("Page number"), QStringLiteral("service"), false},
        {QStringLiteral("seal"), QStringLiteral("Seal"), QStringLiteral("service"), false},
    };
}

}  // namespace

VerificationBlocksModel::VerificationBlocksModel(QObject *parent) : QAbstractListModel(parent) {}

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
    case TypeRole:
        return block.type;
    case NameRole:
        return block.name;
    case GroupRole:
        return block.group;
    case EnabledRole:
        return block.enabled;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> VerificationBlocksModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {TypeRole, "type"},
        {NameRole, "name"},
        {GroupRole, "group"},
        {EnabledRole, "enabled"},
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

int VerificationBlocksModel::rowOfType(const QString &type) const
{
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks.at(i).type == type)
            return i;
    }
    return -1;
}

VerificationPromptStore::VerificationPromptStore(SettingsStore &settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_model(new VerificationBlocksModel(this)), m_contentModel(new BlockGroupFilterModel(this)), m_captionsModel(new BlockGroupFilterModel(this)),
      m_serviceModel(new BlockGroupFilterModel(this))
{
    m_contentModel->setSourceModel(m_model);
    m_contentModel->setGroup(QStringLiteral("content"));
    m_captionsModel->setSourceModel(m_model);
    m_captionsModel->setGroup(QStringLiteral("captions"));
    m_serviceModel->setSourceModel(m_model);
    m_serviceModel->setGroup(QStringLiteral("service"));

    loadBuiltIn();
    loadUser();
    rebuildModel();
}

QString VerificationPromptStore::userPath() const
{
    return QDir(RuntimePaths::fromSettings(m_settings).profilesDir()).filePath(QString::fromUtf8(kUserFileName));
}

QAbstractItemModel *VerificationPromptStore::blockModelContent() const
{
    return m_contentModel;
}

QAbstractItemModel *VerificationPromptStore::blockModelCaptions() const
{
    return m_captionsModel;
}

QAbstractItemModel *VerificationPromptStore::blockModelService() const
{
    return m_serviceModel;
}

int VerificationPromptStore::indexOfType(const QString &type) const
{
    for (int i = 0; i < m_blocks.size(); ++i) {
        if (m_blocks.at(i).type == type)
            return i;
    }
    return -1;
}

VerificationBlock *VerificationPromptStore::findBlock(const QString &type)
{
    const int i = indexOfType(type);
    return i >= 0 ? &m_blocks[i] : nullptr;
}

bool VerificationPromptStore::isTypeEnabled(const QString &type) const
{
    // The model is the single source of truth: the settings window edits it
    // live, and loadValues()/save() keep it in step with the user file.
    for (const VerificationBlock &block : m_model->blocks()) {
        if (block.type == type)
            return block.enabled;
    }
    return false;
}

QSet<QString> VerificationPromptStore::enabledTypes() const
{
    QSet<QString> out;
    for (const VerificationBlock &block : m_model->blocks()) {
        if (block.enabled)
            out.insert(block.type);
    }
    return out;
}

void VerificationPromptStore::loadBuiltIn()
{
    m_blocks = builtInBlocks();
}

void VerificationPromptStore::loadUser()
{
    QFile userFile(userPath());
    if (!userFile.exists())
        return;

    QJsonParseError parseError{};
    if (!userFile.open(QIODevice::ReadOnly)) {
        qWarning("VerificationPromptStore: cannot open user prompts %s: %s", qUtf8Printable(userPath()), qUtf8Printable(userFile.errorString()));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(userFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning("VerificationPromptStore: cannot parse user prompts %s: %s "
                 "(falling back to the built-in defaults)",
                 qUtf8Printable(userPath()),
                 qUtf8Printable(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("schemaVersion"))) {
        const int version = root.value(QStringLiteral("schemaVersion")).toInt(-1);
        if (version > kSchemaVersion) {
            qWarning("VerificationPromptStore: user prompts %s use unsupported "
                     "schema version %d (supported: %d); applying what can be parsed",
                     qUtf8Printable(userPath()),
                     version,
                     kSchemaVersion);
        }
    }

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
}

void VerificationPromptStore::save()
{
    const QList<VerificationBlock> builtIn = builtInBlocks();

    QJsonArray changedBlocks;
    for (const VerificationBlock &block : m_model->blocks()) {
        bool differs = true;
        for (const VerificationBlock &base : builtIn) {
            if (base.type != block.type)
                continue;
            differs = base.enabled != block.enabled;
            break;
        }
        if (differs) {
            QJsonObject obj{{QStringLiteral("type"), block.type}};
            obj.insert(QStringLiteral("enabled"), block.enabled);
            changedBlocks.append(obj);
        }
    }

    if (changedBlocks.isEmpty()) {
        QString error;
        if (!ProfileStorage::removeFileIfExists(userPath(), &error))
            qWarning("VerificationPromptStore: cannot remove user prompts %s: %s", qUtf8Printable(userPath()), qUtf8Printable(error));
        return;
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    root.insert(QStringLiteral("blocks"), changedBlocks);

    QString error;
    if (!ProfileStorage::writeJsonAtomic(userPath(), root, &error))
        qWarning("VerificationPromptStore: cannot write user prompts %s: %s", qUtf8Printable(userPath()), qUtf8Printable(error));
}

void VerificationPromptStore::resetToDefaults()
{
    QString error;
    if (!ProfileStorage::removeFileIfExists(userPath(), &error))
        qWarning("VerificationPromptStore: cannot remove user prompts %s: %s", qUtf8Printable(userPath()), qUtf8Printable(error));
    loadValues();
}

}  // namespace llocr
