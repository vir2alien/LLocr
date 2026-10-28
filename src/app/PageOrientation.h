#pragma once

#include <QImage>

namespace llocr {

// What the picture alone says about the way a page lies, in two cheap
// stages.
//
// The picture is first cropped to its ink — the scanner frame, the dark
// edges and the shadow of the spine stay outside — and the threshold
// follows the illumination (a local Sauvola threshold, not one global
// Otsu), so a gradient or a shadow across the page does not cut it into
// black halves. The skew is then measured by sweeping ±15° for the
// rotation that sharpens the line profile most.
//
// The quadrant comes from the periodicity of the profiles, not from their
// variance. A page of text has a periodic line profile — line, gap, line —
// with the pitch of the leading, while the cross profile stays smooth; a
// page on its side has it the other way round. Variance cannot tell that
// apart from the slow profile of the margins (which is why pages of tables
// and two-column layouts used to stay undecided), it says nothing about how
// firm the answer is, and a skew of two or three degrees smears the lines
// enough to erase it. Periodicity gives an absolute 0..1 measure — the
// confidence — and is read after the deskew, so a small skew costs it
// nothing.
//
// Which way is up is NOT read here. The typographic signs of it — ascenders
// against descenders — carry the sign of the alphabet: Russian has more
// descenders than ascenders, so the same measurement points the other way
// round than in English. A sign that turns with the alphabet turns the pages
// that lie the right way up: measured on the corpus, the reading decided in
// 23 cases and was wrong in 16 of them. The page is left to the user instead.
//
// 180° is not read either: the projections do not change under a half-turn
// at all. Reading it needs a model stage, which waits for a runtime to run
// it on.
struct OrientationVerdict {
    bool textLike = false;   ///< The page carries text at all.
    bool axisKnown = false;  ///< The axis of the text was read.
    bool sideways = false;   ///< The text runs up and down: the page lies on its side.
    /// The rotation that levels the text lines; the page itself lies skewed
    /// by the opposite angle. Positive turns the picture clockwise on
    /// screen. Meaningful once axisKnown.
    double deskewDegrees = 0.0;
    /// Periodicity of the winning axis, 0..1: how firm the axis answer is.
    double confidence = 0.0;
};

// The orientation of a page, read from the picture alone: no model, no
// network, no waiting.
OrientationVerdict detectPageOrientation(const QImage &image);

}  // namespace llocr
