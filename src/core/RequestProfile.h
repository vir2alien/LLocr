#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>
#include <QVariant>

class QJsonArray;
class QJsonObject;

namespace llocr {

// The request field that carries the reasoning budget. llama.cpp reads it per
// request (it overrides the server's --reasoning-budget), and the budget is
// also the switch: zero means the reply is the answer, without thinking
// (ADR 193).
inline constexpr const char *kReasoningBudgetParameter = "reasoning_budget_tokens";

enum class RequestValueKind {
    Number,      // QVariant(double)
    Boolean,     // QVariant(bool)
    String,      // QVariant(QString)
    StringList,  // QVariant(QStringList) — JSON array of strings
};

// A named option of a parameter: the label the settings show and the value
// the request carries. A parameter with choices is a closed set — the
// reasoning mode, for one (the choices come from the request profile).
struct RequestParameterChoice {
    QString label;
    QVariant value;

    bool operator==(const RequestParameterChoice &other) const { return label == other.label && value == other.value; }
    bool operator!=(const RequestParameterChoice &other) const { return !(*this == other); }
};

struct RequestParameter {
    QString name;
    int order = 0;
    RequestValueKind kind = RequestValueKind::Number;
    QVariant value;
    QString description;
    QList<RequestParameterChoice> choices;

    bool operator==(const RequestParameter &other) const;
    bool operator!=(const RequestParameter &other) const { return !(*this == other); }
};

struct RequestProfile {
    QString id;
    QList<RequestParameter> parameters;

    static RequestProfile fromJson(const QJsonObject &root, QString &error);
    QJsonObject toJson() const;
    static QList<RequestProfile> profilesFromJson(const QJsonObject &root, QString &error);
    static QList<RequestParameter> parseParameters(const QJsonArray &array, QString &error, const QString &what);

    static RequestProfile merge(const RequestProfile &defaults, const RequestProfile &user);

    bool operator==(const RequestProfile &other) const;
    bool operator!=(const RequestProfile &other) const { return !(*this == other); }

    void sortByOrder();
    static QJsonValue valueToJson(const QVariant &value);
    static bool valueFromJson(const QJsonValue &value, RequestValueKind &kind, QVariant &out);

    static QString valueToText(const QVariant &value);
    static bool textToValue(const QString &text, RequestValueKind kind, QVariant &out);
};

}  // namespace llocr
