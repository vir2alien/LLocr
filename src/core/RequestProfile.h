#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QJsonValue>

class QJsonObject;

namespace llocr {

/// JSON kind of a request-parameter value. Numbers keep the single JSON
/// number kind (no int/double split): the request profile is user-editable
/// free text and strictness beyond the JSON kind is not required.
enum class RequestValueKind
{
    Number,     // QVariant(double)
    Boolean,    // QVariant(bool)
    String,     // QVariant(QString)
    StringList  // QVariant(QStringList) — JSON array of strings
};

/// One named parameter of the OCR request body. `order` is the parameter's
/// position inside the request profile (1-based); the profile is the single
/// source of the body layout.
struct RequestParameter
{
    QString name;
    int order = 0;
    RequestValueKind kind = RequestValueKind::Number;
    QVariant value;

    bool operator==(const RequestParameter &other) const;
    bool operator!=(const RequestParameter &other) const { return !(*this == other); }
};

/// Request-body profile: an ordered list of parameters appended after the
/// fixed head (`model`, `messages`) in OpenAiProvider::buildRequestBody.
/// Two sources are merged by name: a built-in profile shipped read-only in
/// the Qt resources (:/profiles/request.json) and an optional user profile at
/// <AppData>/LLocr/profiles/request.json (the user profile wins per name;
/// built-in parameters missing from the user file stay, so new built-in
/// parameters appear automatically — § request-profiles decision).
struct RequestProfile
{
    QList<RequestParameter> parameters;

    static constexpr const char *kBuiltInPath = ":/profiles/request.json";

    /// Parses a `{ schemaVersion, parameters: [ { order, name, value } ] }`
    /// object. Returns an empty profile and a non-empty `error` on failure.
    static RequestProfile fromJson(const QJsonObject &root, QString &error);

    /// Serializes to the built-in-style JSON object.
    QJsonObject toJson() const;

    /// Merges by name: defaults first (in their order), each overridden by the
    /// user value when present; user-only parameters are appended after the
    /// built-in ones, sorted by their own order.
    static RequestProfile merge(const RequestProfile &defaults,
                                const RequestProfile &user);

    /// Compares parameter (name, kind, value) triples in order-sorted order.
    bool operator==(const RequestProfile &other) const;
    bool operator!=(const RequestProfile &other) const { return !(*this == other); }

    /// Stable-sorts the parameters by `order`.
    void sortByOrder();

    // --- Value conversion helpers shared by the profile file, the request
    // body and the UI table ---

    static QJsonValue valueToJson(const QVariant &value);
    /// Returns false for unsupported JSON values (object, null, mixed arrays).
    static bool valueFromJson(const QJsonValue &value,
                              RequestValueKind &kind, QVariant &out);

    /// Human-readable text for the UI table.
    static QString valueToText(const QVariant &value);
    /// Parses UI text strictly by kind: Number accepts only a finite number
    /// literal, Boolean only true/false, StringList is a comma-separated list
    /// of strings. Returns false (out untouched) on a format mismatch.
    static bool textToValue(const QString &text, RequestValueKind kind,
                            QVariant &out);
};

}  // namespace llocr
