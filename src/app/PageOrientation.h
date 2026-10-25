#pragma once

#include <QImage>

namespace llocr {

// What the orientation check found out about a page.
struct OrientationVerdict {
    int degrees = 0;        ///< The clockwise turn that brings the page's text upright.
    double confidence = 0;  ///< 0..1 — how sure the check is.
    bool decided = false;   ///< False: the page does not look like a text page, or the check is not sure.
    bool textLike = false;  ///< The page carries text at all.
};

// The orientation of a page, read from the picture alone: no model, no
// network, no waiting.
//
// The text's direction gives the axis — a page of horizontal text has many
// lines and few columns, a page turned on its side has it the other way
// round. Which way is up is read from the shape of the lines: in upright text
// the space between two lines holds more of the next line's ascenders than of
// the previous line's descenders, because the alphabets have more ascenders
// than descenders.
//
// The second reading is a typographic heuristic, not a certainty: a page that
// the check is not sure about comes back undecided, and a model settles it.
OrientationVerdict detectPageOrientation(const QImage &image);

}  // namespace llocr
