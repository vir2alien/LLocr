#pragma once

#include <QImage>
#include <QList>

namespace llocr {

// What the picture alone says about the way a page lies.
//
// The picture reads the axis of the text: a page of horizontal text has many
// lines and few columns, so the profile of the rows alternates (line, gap,
// line, gap) and varies much more than the profile of the columns. A page
// turned on its side has it the other way round. Measured on the corpus of
// the project (ten pages, four turns each): the axis was read in 36 of the
// 40 cases, and right in every one of them (the page of tables reads both
// ways and stays undecided).
//
// Which way is up is NOT read here. The typographic signs of it — ascenders
// against descenders — carry the sign of the alphabet: Russian has more
// descenders than ascenders, so the same measurement points the other way
// round than in English. A sign that turns with the alphabet turns the pages
// that lie the right way up; the model settles the way up instead.
struct OrientationVerdict {
    bool textLike = false;   ///< The page carries text at all.
    bool axisKnown = false;  ///< The axis of the text was read.
    bool sideways = false;   ///< The text runs up and down: the page lies on its side.
};

// The turns a page may need, given what the picture said: a page whose text
// runs across lies straight (0 or 180 degrees), a page on its side lies at a
// quarter turn (90 or 270). A page the picture could not read keeps all four.
QList<int> orientationCandidates(const OrientationVerdict &verdict);

// The orientation of a page, read from the picture alone: no model, no
// network, no waiting.
OrientationVerdict detectPageOrientation(const QImage &image);

}  // namespace llocr
