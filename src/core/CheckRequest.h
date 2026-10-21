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

    QImage image;                        ///< Crop of the selected block being recognized.
    QString systemPrompt;                ///< System-level instruction for the checking model.
    QString typePrompt;                  ///< Instruction for this block's type (from the verification prompts).
    QString recognizedText;              ///< Fix task: the OCR candidate under correction.
    QString blockType;                   ///< Fix task: the block label, picks the reply mode and edit gate.
    QString modelId;                     ///< Model to route the request to (from the connection).
    QList<RequestParameter> parameters;  ///< Sampling parameters from the task's request profile.
    Task task = Task::BlockRecognition;
};

}  // namespace llocr
