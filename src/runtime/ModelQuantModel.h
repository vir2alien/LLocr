#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QString>

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

    enum Roles {
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

    static bool matchesRole(const ModelEntry &entry, const SettingsStore &settings, const QString &role);
    static QString displayTitle(const ModelEntry &entry);

signals:
    void countChanged();

private:
    QList<Row> buildRows() const;
    int selectedIndex(const Row &row) const;
    const Row *rowFor(const QString &key) const;
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
