# Third-Party Notices

LLocr is licensed under the **GNU General Public License v3.0**. This document
lists the third-party software that LLocr uses, bundles, or links against, and
the licenses that govern it. Each component's full license text is kept in the
[`licenses/`](licenses/) directory.

| Component      | Used for                                | License     | License text                   |
| -------------- | --------------------------------------- | ----------- | ------------------------------ |
| Qt             | Core / GUI app framework (incl. Qt PDF) | LGPL-3.0    | [`licenses/Qt-LGPLv3`](licenses/Qt-LGPLv3) |
| Qt WebEngine   | Markdown/LaTeX preview                    | LGPL-3.0    | [`licenses/Qt-LGPLv3`](licenses/Qt-LGPLv3) |
| Chromium       | Bundled inside Qt WebEngine             | BSD-3-Clause| [`licenses/Chromium-LICENSE.txt`](licenses/Chromium-LICENSE.txt) |
| marked         | Markdown → HTML in the preview          | MIT         | [`licenses/marked-LICENSE.txt`](licenses/marked-LICENSE.txt) |
| KaTeX          | LaTeX rendering in the preview          | MIT         | [`licenses/KaTeX-LICENSE.txt`](licenses/KaTeX-LICENSE.txt) |

---

## Qt

- **Project:** Qt (https://www.qt.io/)
- **License:** GNU Lesser General Public License v3.0 (LGPL-3.0)
- **Module of Qt:** LLocr links against `Qt6::Core`, `Qt6::Gui`, `Qt6::Network`,
  `Qt6::Concurrent`, `Qt6::Qml`, `Qt6::Quick`, `Qt6::QuickControls2`,
  `Qt6::Pdf`, and `Qt6::WebEngineQuick`.
- **Usage:** Qt libraries are linked **dynamically**, as required by the LGPL v3.
- **License text:** [`licenses/Qt-LGPLv3`](licenses/Qt-LGPLv3)

> Note: the `Qt-LGPLv3` file contains the LGPL v3 terms that apply to the Qt
> libraries. Qt is also available under alternative commercial and open-source
> (GPLv3) licenses; see the text for details.

---

## Qt WebEngine

- **Project:** Qt WebEngine (part of the Qt framework)
- **License:** LGPL-3.0
- **Usage:** Renders the in-app Markdown/LaTeX preview. It is an optional Qt
  module and pulls in the Chromium browser engine.
- **License text:** [`licenses/Qt-LGPLv3`](licenses/Qt-LGPLv3)

---

## Chromium

- **Project:** Chromium (https://www.chromium.org/)
- **License:** BSD 3-Clause
- **Usage:** Chromium is bundled inside Qt WebEngine, which LLocr uses for the
  Markdown/LaTeX preview. LLocr does not install Chromium separately.
- **Copyright:** Copyright 2015 The Chromium Authors / Google LLC and contributors.
- **License text:** [`licenses/Chromium-LICENSE.txt`](licenses/Chromium-LICENSE.txt)

---

## marked

- **Project:** marked (https://github.com/markedjs/marked)
- **License:** MIT
- **Usage:** Converts Markdown to HTML in the preview. Distributed as
  `resources/preview/marked.min.js`.
- **Copyright:**
  - Copyright (c) 2018+ MarkedJS (https://github.com/markedjs/)
  - Copyright (c) 2011-2018 Christopher Jeffrey
- **License text:** [`licenses/marked-LICENSE.txt`](licenses/marked-LICENSE.txt)

---

## KaTeX

- **Project:** KaTeX (https://katex.org/)
- **License:** MIT
- **Usage:** Renders LaTeX math in the preview. Distributed under
  `resources/preview/` (`katex.min.js`, `katex.min.css`, fonts, and the
  `auto-render` contrib plugin), with a copy of its license at
  `resources/preview/KaTeX-LICENSE.txt`.
- **Copyright:** Copyright (c) 2013-2020 Khan Academy and other contributors.
- **License text:** [`licenses/KaTeX-LICENSE.txt`](licenses/KaTeX-LICENSE.txt)