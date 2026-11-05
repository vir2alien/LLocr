#pragma once

#include <QHash>
#include <QImage>
#include <QSize>
#include <QString>

#include "core/ModelProfiles.h"

namespace llocr {

// Crop preprocessing before the image is encoded into a request (ADR 287).
// The profile decides per role which stages run: the glyph stage stretches a
// crop whose measured letter height is below the block label's target (the
// fix/read formula scale), and the vision-token budget stage shrinks a crop
// whose estimated token count exceeds the budget (the block task's guard
// against dense-table prompts). Labels outside the map and a budget of 0
// leave the crop untouched.
struct CropScalingPolicy {
    int maxVisionTokens = 0;                 // 0 = the budget stage is off
    QHash<QString, int> glyphHeightByBlock;  // empty = the glyph stage is off
};

CropScalingPolicy policyForRole(const ModelProfiles::Role *role);

// The glyph stage only stretches, the budget stage only shrinks; a crop that
// passes both is the answer.
QImage applyCropScaling(const QImage &crop, const QString &blockLabel, const CropScalingPolicy &policy);

// The dynamic-resolution token estimate the budget stage scales against: the
// encoder splits the crop into patch-size cells and merges 2x2 of them into
// one token. Calibrated on MinerU2.5-Pro prompt tokens (within ~1% on crops
// of a thousand tokens and more).
int estimateVisionTokens(const QSize &pixelSize);

// The crop's letter height: the median of the letter-like connected
// components. 0 = nothing letter-like was found (blank, photograph, rule) —
// unlike a measured height, 0 never scales the crop.
int cropGlyphHeight(const QImage &crop);

}  // namespace llocr
