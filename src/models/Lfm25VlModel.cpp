#include "models/Lfm25VlModel.h"

namespace llocr {

namespace {

// The official layout-annotation prompt from the LFM2.5-VL model card.
// Sent verbatim: with a single page image the model labels its regions
// image_index=0, which is exactly what the parser expects.
const char *kLayoutPrompt = "Parse this document into its layout regions. The pages are provided as "
                            "images in reading order. For every region, in reading order across all "
                            "pages, output a header line immediately followed by the region's "
                            "content:\n\n"
                            "image_index=<n> <label> [xmin, ymin, xmax, ymax]\n"
                            "<content>\n\n"
                            "where:\n"
                            "- image_index is the zero-based index of the page image the region "
                            "appears on (0 for the first image, 1 for the second, and so on)\n"
                            "- <label> is one of these layout labels: text, title, list, table, "
                            "table_caption, table_footnote, image, image_block, image_caption, "
                            "image_footnote, chart, equation, formula_number, code, code_caption, "
                            "algorithm, aside_text, ref_text, phonetic, page_header, page_footer, "
                            "page_number, page_footnote\n"
                            "- [xmin, ymin, xmax, ymax] are normalized integer coordinates in "
                            "[0, 1000]\n"
                            "- <content> is the region's content: plain text for text regions, "
                            "LaTeX for equations, OTSL for tables, and a short description for "
                            "images and charts\n\n"
                            "Separate each region block with one blank line. Return only the parsed "
                            "regions.";

}  // namespace

QString Lfm25VlModel::id() const
{
    return QStringLiteral("lfm25-vl-3b");
}

QString Lfm25VlModel::displayName() const
{
    return QStringLiteral("LFM2.5-VL-3B");
}

QList<OcrPromptVariant> Lfm25VlModel::promptVariants() const
{
    return {OcrPromptVariant{QStringLiteral("layout-annotation"), QStringLiteral("Layout annotation"), QString::fromUtf8(kLayoutPrompt)}};
}

QString Lfm25VlModel::defaultParserId() const
{
    return QStringLiteral("det_tokens");
}

}  // namespace llocr
