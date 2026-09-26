#pragma once

#include <QString>

namespace llocr {

// LFM2.5-VL layout-annotation drift (experimental format, model card): a
// table caption may be emitted as long text inside <label>…</label> with the
// OTSL rows following in <content>…</content>. Rewrites it into two synthetic
// regions (table_caption + table) so both are tokenized and styled properly.
// The short-name shape (<label>image</label>) is left untouched — the token
// regex handles it directly.
QString normalizeDriftRegions(const QString &raw);

} // namespace llocr
