#pragma once

#include <QString>

namespace llocr {

bool containsOtslTable(const QString &text);

QString formatOtslTable(QString text, bool tablesAsHtml);

}  // namespace llocr
