#pragma once

#include <QString>

namespace llocr {

// OTSL (Optimized Table Structure Language, arXiv 2305.03393) — the table
// serialization used by LFM2.5-VL. Token vocabulary (TableFormer-style):
// <fcel> opens a cell, <lcel>/<ucel>/<xcel> are cells covered by a
// left/up/cross span, <nl> ends a row.

// True when the text contains OTSL tokens (in either the clean or the
// LFM2.5-VL drifted shape with closing </fcel> tags).
bool containsOtslTable(const QString &text);

// Converts an OTSL sequence to a GFM pipe table — or, in the tablesAsHtml
// mode (ADR 64), to a real HTML <table> whose span tokens become live
// rowspan/colspan attributes. Per the ADR 19 convention a spanned value is
// written once (into the top-left cell), so covered cells render empty —
// any stray content after a covered token is dropped. A single-column
// fragment (e.g. leaked formula lines) becomes plain lines.
QString formatOtslTable(QString text, bool tablesAsHtml);

} // namespace llocr
