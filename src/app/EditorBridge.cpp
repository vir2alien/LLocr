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

void EditorBridge::notifyBlockChanged(int blockOrdinal, const QString &markdown, qint64 pushToken)
{
    emit editorBlockChanged(blockOrdinal, markdown, pushToken);
}

void EditorBridge::notifyResyncRequested()
{
    emit editorResyncRequested();
}

void EditorBridge::notifyBlockStructureAction(int blockOrdinal, const QString &action)
{
    emit editorBlockStructureRequested(blockOrdinal, action);
}

void EditorBridge::notifyReferenceChanged(const QString &markdown, qint64 pushToken)
{
    emit editorReferenceChanged(markdown, pushToken);
}

void EditorBridge::notifyBlockClicked(int blockIndex)
{
    emit editorBlockClicked(blockIndex);
}

}  // namespace llocr
