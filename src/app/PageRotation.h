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

// The rectangle the page fills after the straightening: the turned rectangle
// grown to the bounding box of the turn. The size the deskewImage produces.
QRect deskewedRect(const QSize &size, double degrees);

// The size the page is shown in after the straightening.
QSize deskewedSize(const QSize &size, double degrees);

// The straightening itself: the page turned by a fraction of a degree — the
// skew the orientation check measured — with a smooth resample over paper.
// An angle below the fraction of a degree the check cannot resolve leaves
// the image alone.
QImage deskewImage(const QImage &image, double degrees);

}  // namespace llocr
