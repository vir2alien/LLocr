#pragma once

#include <QByteArray>
#include <QString>

namespace llocr {

QByteArray sha256File(const QString &path, bool *ok = nullptr);

}  // namespace llocr