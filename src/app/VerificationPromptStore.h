#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

namespace llocr {

class SettingsStore;
class BlockGroupFilterModel;

struct VerificationBlock {
    QString type;        ///< Canonical block label (text, title, table, ...).
    QString name;        ///< Human-readable name for the UI.
    QString group;       ///< UI grouping: content / captions / service.
    bool enabled = true; ///< Whether this block type is auto-verified.
    QString prompt;      ///< Type-specific instruction used by the verifier.
};

class VerificationBlocksModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY countsChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY countsChanged)

public:
    enum Roles {
        TypeRole = Qt::UserRole + 1,
        NameRole,
        GroupRole,
        EnabledRole,
        PromptRole,
    };

    explicit VerificationBlocksModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void resetFrom(const QList<VerificationBlock> &blocks);
    Q_INVOKABLE void setEnabled(int row, bool on);
    Q_INVOKABLE void setAllEnabled(bool on);
    Q_INVOKABLE void setPrompt(int row, const QString &text);
    const QList<VerificationBlock> &blocks() const { return m_blocks; }

    int enabledCount() const;
    int totalCount() const { return m_blocks.size(); }
    int revision() const { return m_revision; }

    Q_INVOKABLE QString typeAt(int row) const;
    Q_INVOKABLE QString nameAt(int row) const;
    Q_INVOKABLE bool enabledAt(int row) const;
    Q_INVOKABLE QString promptAt(int row) const;
    // Source-model row for a block type; -1 when unknown. Needed because
    // Q_INVOKABLEs do not pass through QSortFilterProxyModel, while the UI
    // works with group-filtered views.
    Q_INVOKABLE int rowOfType(const QString &type) const;

signals:
    void countsChanged();

private:
    QList<VerificationBlock> m_blocks;
    int m_revision = 0;
};

class VerificationPromptStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY systemPromptChanged)
    Q_PROPERTY(QObject* blockModel READ blockModel CONSTANT)
    // Group-filtered views over blockModel for the three-column UI
    // (content / captions / service). CONSTANT is safe: the source model
    // lives as long as the store.
    Q_PROPERTY(QAbstractItemModel* blockModelContent READ blockModelContent CONSTANT)
    Q_PROPERTY(QAbstractItemModel* blockModelCaptions READ blockModelCaptions CONSTANT)
    Q_PROPERTY(QAbstractItemModel* blockModelService READ blockModelService CONSTANT)

public:
    explicit VerificationPromptStore(SettingsStore &settings, QObject *parent = nullptr);

    QString systemPrompt() const { return m_systemPrompt; }
    void setSystemPrompt(const QString &text);

    QAbstractListModel *blockModel() const { return m_model; }
    QAbstractItemModel *blockModelContent() const;
    QAbstractItemModel *blockModelCaptions() const;
    QAbstractItemModel *blockModelService() const;

    QStringList blockTypes() const;
    QString promptForType(const QString &type) const;
    bool isTypeEnabled(const QString &type) const;

    Q_INVOKABLE void loadValues();      // re-read built-in + user file (discard edits)
    Q_INVOKABLE void save();            // persist user overrides
    Q_INVOKABLE void resetToDefaults(); // drop user file, re-read built-in
    Q_INVOKABLE QString originalPromptAt(int row) const;
    Q_INVOKABLE QString originalSystemPrompt() const { return m_originalSystemPrompt; }

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
    QString m_originalSystemPrompt;
    QList<VerificationBlock> m_blocks;
    QHash<QString, QString> m_originalPrompts;
    VerificationBlocksModel *m_model;
    BlockGroupFilterModel *m_contentModel;
    BlockGroupFilterModel *m_captionsModel;
    BlockGroupFilterModel *m_serviceModel;
};

}  // namespace llocr