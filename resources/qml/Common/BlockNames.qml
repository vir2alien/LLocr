pragma Singleton

import QtQuick

QtObject {
    readonly property var nameMap: ({
        "Text": qsTr("Text"),
        "Title": qsTr("Headings"),
        "Table": qsTr("Tables"),
        "Equation": qsTr("Equations"),
        "Formula": qsTr("Formulas"),
        "List": qsTr("Lists"),
        "Code": qsTr("Code"),
        "Abstract": qsTr("Abstracts"),
        "Image caption": qsTr("Image captions"),
        "Table caption": qsTr("Table captions"),
        "Figure footnote": qsTr("Figure footnotes"),
        "Table footnote": qsTr("Table footnotes"),
        "Reference text": qsTr("Reference texts"),
        "Reference": qsTr("References"),
        "Header": qsTr("Running headers"),
        "Footer": qsTr("Running footers"),
        "Page number": qsTr("Page numbers"),
        "Seal": qsTr("Seals and stamps")
    })

    readonly property var hintMap: ({
        "Equation": qsTr("numbered display equations"),
        "Formula": qsTr("isolated formulas (LaTeX)")
    })

    function displayName(name, type) {
        return nameMap[name] || (name.length > 0 ? name : type)
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
