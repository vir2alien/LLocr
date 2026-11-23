pragma Singleton

import QtQuick

QtObject {
    // The block vocabulary: the display name (translated) and the label the
    // app stores on a block. The names double as the keys of the verification
    // block types, so they are the C++ names of the built-in list.
    readonly property var blockTypes: ({
        "Text": { name: qsTr("Text"), type: "text" },
        "Title": { name: qsTr("Headings"), type: "title" },
        "Table": { name: qsTr("Tables"), type: "table" },
        "Equation": { name: qsTr("Equations"), type: "equation" },
        "Formula": { name: qsTr("Formulas"), type: "formula" },
        "List": { name: qsTr("Lists"), type: "list" },
        "Code": { name: qsTr("Code"), type: "code" },
        "Abstract": { name: qsTr("Abstracts"), type: "abstract" },
        "Key-value field": { name: qsTr("Key-value fields"), type: "key_value" },
        "Image": { name: qsTr("Images"), type: "image" },
        "Image caption": { name: qsTr("Image captions"), type: "image_caption" },
        "Table caption": { name: qsTr("Table captions"), type: "table_caption" },
        "Figure footnote": { name: qsTr("Figure footnotes"), type: "figure_footnote" },
        "Table footnote": { name: qsTr("Table footnotes"), type: "table_footnote" },
        "Reference text": { name: qsTr("Reference texts"), type: "ref_text" },
        "Reference": { name: qsTr("References"), type: "reference" },
        "Header": { name: qsTr("Running headers"), type: "header" },
        "Footer": { name: qsTr("Running footers"), type: "footer" },
        "Page number": { name: qsTr("Page numbers"), type: "page_number" },
        "Seal": { name: qsTr("Seals and stamps"), type: "seal" }
    })

    readonly property var nameMap: {
        const out = {}
        for (const key in blockTypes)
            out[key] = blockTypes[key].name
        return out
    }

    // The name of a block by the label the model carries. The models speak
    // their own vocabulary («equation», «image_caption»); the operator reads
    // a name.
    readonly property var labelNames: ({
        "text": qsTr("Text"),
        "title": qsTr("Heading"),
        "doc_title": qsTr("Document title"),
        "paragraph_title": qsTr("Paragraph heading"),
        "abstract": qsTr("Abstract"),
        "table": qsTr("Table"),
        "table_title": qsTr("Table title"),
        "table_caption": qsTr("Table caption"),
        "table_footnote": qsTr("Table footnote"),
        "equation": qsTr("Equation"),
        "formula": qsTr("Formula"),
        "inline_formula": qsTr("Inline formula"),
        "formula_number": qsTr("Formula number"),
        "image": qsTr("Image"),
        "chart": qsTr("Chart"),
        "figure": qsTr("Figure"),
        "image_caption": qsTr("Image caption"),
        "figure_caption": qsTr("Figure caption"),
        "image_footnote": qsTr("Image footnote"),
        "caption": qsTr("Caption"),
        "code": qsTr("Code"),
        "code_caption": qsTr("Code caption"),
        "algorithm": qsTr("Algorithm"),
        "list": qsTr("List"),
        "list_item": qsTr("List item"),
        "index": qsTr("Index"),
        "phonetic": qsTr("Transcription"),
        "aside_text": qsTr("Aside"),
        "ref_text": qsTr("Reference text"),
        "reference": qsTr("Reference"),
        "key_value": qsTr("Key-value field"),
        "header": qsTr("Running header"),
        "footer": qsTr("Running footer"),
        "page_number": qsTr("Page number"),
        "seal": qsTr("Seal")
    })

    // The name of a block by the label a model carries. An unknown label
    // stays as it is: nothing is lost, and the panel never shows an empty
    // name.
    function labelName(label) {
        if (!label)
            return ""
        return labelNames[label] !== undefined ? labelNames[label] : label
    }

    // The display name of a block type, by the C++ name the model carries.
    function displayName(name, type) {
        return nameMap[name] || (name.length > 0 ? name : type)
    }

    // The label a block of this type carries.
    function blockType(name) {
        return blockTypes[name] ? blockTypes[name].type : "text"
    }

    function groupTitle(group) {
        if (group === "content")
            return qsTr("Main content")
        if (group === "captions")
            return qsTr("Captions and footnotes")
        if (group === "service")
            return qsTr("Service")
        return group
    }
}
