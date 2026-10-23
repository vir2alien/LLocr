#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

#include "core/ModelProfiles.h"
#include "runtime/ModelRegistry.h"

namespace llocr {

class ModelInstaller;
class SettingsStore;

class ModelQuantModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    struct Quant {
        QString id;            ///< display label, e.g. "Q4_K_M"
        int entryIndex = -1;   ///< the installer's registry list, -1 when not installed
        int presetIndex = -1;  ///< the role's preset list, -1 when the profile offers no download
        qint64 size = 0;
        qint64 vramBytes = 0;  ///< the card the quantization needs (ADR 191)
        bool active = false;
    };

    struct Row {
        QString key;  ///< profile id, or the registry entry id of a standalone model
        QString title;
        QString subtitle;  ///< the Hugging Face repo, or the folder of a standalone model
        QString license;
        QString runtimeNote;
        bool profile = false;
        QList<Quant> quants;
        int selected = 0;
    };

    enum Roles : int {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        ProfileRole,
        LicenseRole,
        RuntimeNoteRole,
        QuantsRole,
        SelectedRole,
        SelectedLabelRole,
        SelectedInstalledRole,
        SelectedActiveRole,
        SelectedSizeRole,
        SelectedDownloadableRole,
        ActiveRole,
    };
    Q_ENUM(Roles)

    ModelQuantModel(ModelInstaller &installer, SettingsStore &settings, QString role, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void refresh();

    int entryIndexFor(const QString &key, const QString &quantId) const;
    QList<int> entryIndexesFor(const QString &key) const;
    int presetIndexFor(const QString &key, const QString &quantId) const;
    bool hasQuant(const QString &key, const QString &quantId) const { return quantFor(key, quantId) != nullptr; }

    // Task-window access to one row by profile id (the quant combo binds to
    // the row's quants; the invokables are re-read when the caller resyncs).
    Q_INVOKABLE int rowIndexOf(const QString &key) const;
    Q_INVOKABLE QVariantMap rowFor(const QString &key) const;

    static bool matchesRole(const ModelEntry &entry, const SettingsStore &settings, const QString &role);
    static QString displayTitle(const ModelEntry &entry);
    // File identity across multi-part names: "model-00001-of-00002.gguf" and
    // the quantization leaf both reduce to "model".
    static QString stemName(const QString &pathOrFile);

signals:
    void countChanged();

private:
    QList<Row> buildRows() const;
    // The card the quantization needs: the weights, the projector, the draft,
    // the KV cache, the working set and the vision encoder (ADR 191).
    qint64 requiredVram(const ModelProfiles::Profile &profile, qint64 weightsBytes, qint64 projectorBytes) const;
    int selectedIndex(const Row &row) const;
    const Row *findRow(const QString &key) const;
    const Quant *quantFor(const QString &key, const QString &quantId) const;
    QList<int> allRoles() const;

    static QString signatureOf(const QList<Row> &rows);

    ModelInstaller &m_installer;
    SettingsStore &m_settings;
    QString m_role;  // model-profile role id this list serves
    QList<Row> m_rows;
    QString m_signature;
};

}  // namespace llocr
