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

    readonly property var hintMap: ({
        "Equation": qsTr("numbered display equations"),
        "Formula": qsTr("isolated formulas (LaTeX)")
    })

    function displayName(name, type) {
        return nameMap[name] || (name.length > 0 ? name : type)
    }

    // The label a block of this type carries.
    function blockType(name) {
        return blockTypes[name] ? blockTypes[name].type : "text"
    }

    function hint(type) {
        return hintMap[type] || ""
    }

    function groupTitle(group) {
        if (group === "content")
            return qsTr("Main content")
        if (group === "captions")
            return qsTr("Captions, footnotes and references")
        if (group === "service")
            return qsTr("Service")
        return group
    }
}
