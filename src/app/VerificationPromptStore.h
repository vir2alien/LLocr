#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace llocr {

class SettingsStore;

struct VerificationBlock {
    QString type;        ///< Canonical block label (text, title, table, ...).
    QString name;        ///< Human-readable name for the UI.
    bool enabled = true; ///< Whether this block type is auto-verified.
    QString prompt;      ///< Type-specific instruction used by the verifier.
};

// Rolling list model for the blocks tab: one row per block type, roles expose
// the type name, its display name, the enabled checkbox and the prompt.
class VerificationBlocksModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        TypeRole = Qt::UserRole + 1,
        NameRole,
        EnabledRole,
        PromptRole,
    };

    explicit VerificationBlocksModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void resetFrom(const QList<VerificationBlock> &blocks);
    Q_INVOKABLE void setEnabled(int row, bool on);
    Q_INVOKABLE void setPrompt(int row, const QString &text);
    const QList<VerificationBlock> &blocks() const { return m_blocks; }

    Q_INVOKABLE QString typeAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE bool enabledAt(int row) const;
    Q_INVOKABLE QString promptAt(int row) const;

private:
    QList<VerificationBlock> m_blocks;
};

// Holds the verification prompts: a built-in default set shipped as a resource
// (:/profiles/verifyPrompts.json) plus an optional user override stored in the
// profiles directory (verifyPrompts.json). The user file only contains the
// entries that differ from the built-in defaults, mirroring the request/launch
// profile stores. Exposed to QML as the "Verification" singleton.
class VerificationPromptStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY systemPromptChanged)
    Q_PROPERTY(QObject* blockModel READ blockModel CONSTANT)

public:
    explicit VerificationPromptStore(SettingsStore &settings, QObject *parent = nullptr);

    QString systemPrompt() const { return m_systemPrompt; }
    void setSystemPrompt(const QString &text);

    QAbstractListModel *blockModel() const { return m_model; }

    // Effective (merged) config:
    QStringList blockTypes() const;
    QString promptForType(const QString &type) const;
    bool isTypeEnabled(const QString &type) const;

    Q_INVOKABLE void loadValues();      // re-read built-in + user file (discard edits)
    Q_INVOKABLE void save();            // persist user overrides
    Q_INVOKABLE void resetToDefaults(); // drop user file, re-read built-in

signals:
    void systemPromptChanged();
    void modelChanged();

private:
    QString userPath() const;
    void loadBuiltIn();
    void loadUser();
    void rebuildModel();
    int indexOfType(const QString &type) const;
    const VerificationBlock *findBlock(const QString &type) const;
    VerificationBlock *findBlock(const QString &type);

    SettingsStore &m_settings;
    QString m_systemPrompt;
    QList<VerificationBlock> m_blocks;
    VerificationBlocksModel *m_model;
};

}  // namespace llocr