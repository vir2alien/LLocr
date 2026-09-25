#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QJsonValue>

class QJsonObject;

namespace llocr {

enum class RequestValueKind
{
    Number,     // QVariant(double)
    Boolean,    // QVariant(bool)
    String,     // QVariant(QString)
    StringList, // QVariant(QStringList) — JSON array of strings
};

struct RequestParameter
{
    QString name;
    int order = 0;
    RequestValueKind kind = RequestValueKind::Number;
    QVariant value;
    QString description;

    bool operator==(const RequestParameter &other) const;
    bool operator!=(const RequestParameter &other) const { return !(*this == other); }
};

struct RequestProfile
{
    QString id;
    QList<RequestParameter> parameters;

    static constexpr const char *kBuiltInPath = ":/profiles/requestOcr.json";
    static RequestProfile fromJson(const QJsonObject &root, QString &error);
    QJsonObject toJson() const;
    static QList<RequestProfile> profilesFromJson(const QJsonObject &root,
                                                  QString &error);

    static RequestProfile merge(const RequestProfile &defaults,
                                const RequestProfile &user);

    bool operator==(const RequestProfile &other) const;
    bool operator!=(const RequestProfile &other) const { return !(*this == other); }

    void sortByOrder();
    static QJsonValue valueToJson(const QVariant &value);
    static bool valueFromJson(const QJsonValue &value,
                              RequestValueKind &kind, QVariant &out);

    static QString valueToText(const QVariant &value);
    static bool textToValue(const QString &text, RequestValueKind kind,
                            QVariant &out);
};

}  // namespace llocr
