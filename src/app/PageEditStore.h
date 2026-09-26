#pragma once

#include <QHash>
#include <QString>

#include "app/DocumentModel.h"

namespace llocr {

/// Remembers, per recognized page, the text the page had when it was
/// recognized — and whether it has been changed since.
///
/// It deliberately does **not** hold the current text: the page owns that
/// (`DocumentPage::result.text`, written through AppController's page-text
/// accessor). Holding it in two places is what made a structural edit
/// (removing a block, applying a verified fix) get silently discarded when the
/// user typed the recognized text back into the editor: the comparison was made
/// against the parse-time text, not against what the page actually showed.
///
/// The store is therefore just the revert baseline plus the "edited" marker.
class PageEditStore
{
public:
    /// Called when a page is (re)recognized: drops any previous edit and stores
    /// the new text as the baseline for Revert. Returns true when the page had
    /// an edit before, i.e. the caller must clear the page's "edited" marker.
    bool reset(int index, const QString& recognizedText);

    /// The text the page had when it was last recognized; empty when the page
    /// is unknown.
    QString baseline(int index) const;

    /// Drops the "edited" marker. Returns true when it was set. The caller
    /// restores the page text from baseline() when reverting user edits.
    bool revert(int index);

    bool isEdited(int index) const;
    void setEdited(int index, bool edited);

    void clear();
    void remapAfterRemove(int removedIndex);
    void remapAfterMove(int from, int to);

private:
    QHash<int, QString> m_baselines;
    QHash<int, bool> m_edited;
};

}  // namespace llocr
