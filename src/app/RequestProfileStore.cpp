#include "app/RequestProfileStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "app/SettingsStore.h"
#include "runtime/RuntimePaths.h"

namespace llocr {

RequestProfileStore::RequestProfileStore(SettingsStore &settings,
                                         const QString &builtInPath,
                                         QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_model(new RequestParametersModel(this))
{
    QString error;
    QFile builtIn(builtInPath);
    if (builtIn.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(builtIn.readAll(),
                                                          &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            error = parseError.errorString();
        } else {
            m_defaults = RequestProfile::fromJson(doc.object(), error);
        }
    } else {
        error = builtIn.errorString();
    }
    if (!error.isEmpty())
        qWarning("RequestProfileStore: cannot load built-in profile %s: %s",
                 qUtf8Printable(builtInPath), qUtf8Printable(error));

    reloadActive();
    m_model->resetFrom(m_active.parameters);
}

QString RequestProfileStore::userPath() const
{
    return QDir(RuntimePaths(m_settings.runtimeRootDir(),
                             m_settings.runtimeModelsDir())
                    .profilesDir())
        .filePath(QStringLiteral("request.json"));
}

bool RequestProfileStore::hasUserProfile() const
{
    return QFile::exists(userPath());
}

void RequestProfileStore::reloadActive()
{
    RequestProfile user;
    QFile userFile(userPath());
    if (userFile.exists()) {
        QString error;
        if (userFile.open(QIODevice::ReadOnly)) {
            QJsonParseError parseError{};
            const QJsonDocument doc = QJsonDocument::fromJson(userFile.readAll(),
                                                              &parseError);
            if (parseError.error != QJsonParseError::NoError || !doc.isObject())
                error = parseError.errorString();
            else
                user = RequestProfile::fromJson(doc.object(), error);
        } else {
            error = userFile.errorString();
        }
        if (!error.isEmpty())
            qWarning("RequestProfileStore: cannot load user profile %s: %s "
                     "(falling back to the built-in defaults)",
                     qUtf8Printable(userPath()), qUtf8Printable(error));
    }
    m_active = RequestProfile::merge(m_defaults, user);
}

void RequestProfileStore::reloadDraft()
{
    m_model->resetFrom(m_active.parameters);
}

void RequestProfileStore::loadDefaultDraft()
{
    m_model->resetFrom(m_defaults.parameters);
}

bool RequestProfileStore::setDraftValue(int row, const QString &text)
{
    return m_model->setValue(row, text);
}

void RequestProfileStore::saveDraft()
{
    RequestProfile draft;
    draft.parameters = m_model->parameters();
    draft.sortByOrder();

    if (draft == m_defaults) {
        // A user profile exists only for changed settings.
        QFile userFile(userPath());
        if (userFile.exists() && !userFile.remove())
            qWarning("RequestProfileStore: cannot remove user profile %s: %s",
                     qUtf8Printable(userPath()),
                     qUtf8Printable(userFile.errorString()));
        reloadActive();
        emit profileChanged();
        return;
    }

    QDir().mkpath(QFileInfo(userPath()).absolutePath());
    QSaveFile file(userPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("RequestProfileStore: cannot write user profile %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }
    file.write(QJsonDocument(draft.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("RequestProfileStore: cannot commit user profile %s: %s",
                 qUtf8Printable(userPath()), qUtf8Printable(file.errorString()));
        return;
    }

    reloadActive();
    emit profileChanged();
}

void RequestProfileStore::resetToDefaults()
{
    QFile userFile(userPath());
    if (userFile.exists() && !userFile.remove())
        qWarning("RequestProfileStore: cannot remove user profile %s: %s",
                 qUtf8Printable(userPath()),
                 qUtf8Printable(userFile.errorString()));
    reloadActive();
    loadDefaultDraft();
    emit profileChanged();
}

}  // namespace llocr
