#pragma once

#include <QImage>
#include <QList>
#include <QString>

#include "core/RequestProfile.h"

namespace llocr {

struct CheckRequest {
    // The block-stage tasks the check transport serves. The task decides the
    // wire shape (the fix candidate travels as its own part, ADR 163) and the
    // reply contract (fix answers OK / FIX / REVIEW, the rest transcribe —
    // the layout reply is markup a parser reads).
    enum class Task { BlockRecognition, Fix, Layout };

    // The two reply contracts of the fix task (ADR 259, ADR 260). The fixer
    // corrects the recognized text; the reader writes the block from the image
    // and takes the recognized text only as a sample of the document's
    // notation. The protocol is the action's choice, not the model's: the
    // same model serves both.
    enum class FixProtocol { FullBlock, ReadBlock };

    QImage image;                        ///< Crop of the selected block being recognized.
    QString systemPrompt;                ///< System-level instruction for the checking model.
    QString typePrompt;                  ///< Instruction for this block's type (from the verification prompts).
    QString recognizedText;              ///< Fix task: the OCR candidate under correction.
    QString blockType;                   ///< Fix task: the block label, picks the reply mode and edit gate.
    QString modelId;                     ///< Model to route the request to (from the connection).
    QList<RequestParameter> parameters;  ///< Sampling parameters from the task's request profile.
    Task task = Task::BlockRecognition;
    FixProtocol fixProtocol = FixProtocol::FullBlock;
};

}  // namespace llocr
