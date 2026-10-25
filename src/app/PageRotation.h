#pragma once

#include <QImage>
#include <QSize>

namespace llocr {

// The page's orientation relative to its source: the clockwise turn the
// renderer applies to the source image. Only the quarter turns exist — a page
// is either upright or sideways, and the step keeps every angle on that grid.
inline constexpr int kRotationStep = 90;

// The angle on the quarter-turn grid (0, 90, 180, 270). Any other angle snaps
// to the nearest quarter turn, so a corrupted stored value cannot turn a page
// into an arbitrary skew.
int normalizeRotation(int degrees);

// The size the page has after the turn: the source size transposed for the
// quarter turns.
QSize rotatedSize(const QSize &size, int degrees);

// The turned image. The quarter turns move the pixels, they do not resample
// them: a scan stays sharp, and the geometry the model reports against it
// stays exact.
QImage rotateImage(const QImage &image, int degrees);

}  // namespace llocr
