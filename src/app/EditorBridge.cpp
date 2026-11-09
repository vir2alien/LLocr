#include "app/EditorBridge.h"

namespace llocr {

EditorBridge::EditorBridge(QObject *parent) : QObject(parent)
{
    setObjectName(QStringLiteral("editorBridge"));
}

void EditorBridge::notifyPageReady()
{
    emit pageReady();
}

void EditorBridge::notifyTextChanged(const QString &markdown, qint64 pushToken)
{
    emit editorTextChanged(markdown, pushToken);
}

void EditorBridge::notifyBlockChanged(int blockOrdinal, const QString &markdown, qint64 pushToken)
{
    emit editorBlockChanged(blockOrdinal, markdown, pushToken);
}

void EditorBridge::notifyBlockReleased(int blockOrdinal, qint64 pushToken)
{
    emit editorBlockReleased(blockOrdinal, pushToken);
}

void EditorBridge::notifyReferenceChanged(const QString &markdown, qint64 pushToken)
{
    emit editorReferenceChanged(markdown, pushToken);
}

void EditorBridge::notifyEditorFocus(bool focused)
{
    emit editorFocusChanged(focused);
}

void EditorBridge::notifyBlockClicked(int blockIndex)
{
    emit editorBlockClicked(blockIndex);
}

}  // namespace llocr
