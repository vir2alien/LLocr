#pragma once

#include "models/OcrModel.h"

namespace llocr {

// LiquidAI LFM2.5-VL-3B (https://huggingface.co/LiquidAI/LFM2.5-VL-3B).
// The model answers the layout-annotation prompt with bare det-style tokens:
//   image_index=<n> <label> [xmin, ymin, xmax, ymax]
//   <content>
// which DetTokensParser consumes via its unwrapped-token path (the parser
// skips the image_index prefix; OTSL table content is flattened to a GFM
// pipe table). Tables are serialized in OTSL, not HTML.
class Lfm25VlModel : public OcrModel
{
    Q_DISABLE_COPY_MOVE(Lfm25VlModel)

public:
    Lfm25VlModel() = default;

    QString id() const override;
    QString displayName() const override;
    QList<OcrPromptVariant> promptVariants() const override;
    QString defaultParserId() const override;
};

}  // namespace llocr
