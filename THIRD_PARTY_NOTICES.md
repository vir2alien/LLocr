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
| DOMPurify      | HTML sanitization in the preview        | Apache-2.0 OR MPL-2.0 | [`licenses/DOMPurify-LICENSE.txt`](licenses/DOMPurify-LICENSE.txt) |
| zlib           | ZIP/gzip decompression + CRC-32 in the runtime installer (`ArchiveExtractor`) | zlib License | [`licenses/zlib-LICENSE.txt`](licenses/zlib-LICENSE.txt) |
| DjVuLibre      | DjVu document decoding through `ddjvuapi` | GPL-2.0-or-later | [`licenses/DjVuLibre-COPYING`](licenses/DjVuLibre-COPYING) |

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

---

## DOMPurify

- **Project:** DOMPurify (https://github.com/cure53/DOMPurify)
- **License:** Apache License 2.0 OR Mozilla Public License 2.0
- **Usage:** Sanitizes the HTML produced by marked (and the math-restored
  markup) before it is inserted into the preview DOM, so untrusted text from
  OCR/LLM output cannot execute scripts. Distributed under
  `resources/preview/purify.min.js`, version 3.2.4.
- **Copyright:** Copyright (c) Cure53 and other contributors.
- **License text:** [`licenses/DOMPurify-LICENSE.txt`](licenses/DOMPurify-LICENSE.txt)

---

## zlib

- **Project:** zlib (https://zlib.net/)
- **License:** zlib License
- **Usage:** The runtime installer's `ArchiveExtractor` uses zlib for ZIP
  deflate/inflate (`method 8`) with CRC-32 verification and for `.tar.gz` /
  gzip decompression of downloaded llama.cpp release archives (ADR 34).
- **Source of the library:** resolved by `find_package(ZLIB REQUIRED)`. On
  **Windows** it comes from **vcpkg** (triplet `x64-windows`, currently
  v1.3.x at `C:/vcpkg/installed/x64-windows`); on macOS/Linux the system zlib
  is used. It is **not** bundled into the LLocr source tree.
- **Copyright:** Copyright (C) 1995-2026 Jean-loup Gailly and Mark Adler.
- **License text:** [`licenses/zlib-LICENSE.txt`](licenses/zlib-LICENSE.txt)

---

## DjVuLibre

- **Project:** DjVuLibre (https://djvu.sourceforge.net/),
  [upstream source](https://github.com/DjVuLibre/djvulibre).
- **License:** GNU General Public License **version 2 or any later version**
  (`GPL-2.0-or-later`), not GPL-2.0-only. The public
  [`libdjvu/ddjvuapi.h`](https://github.com/DjVuLibre/djvulibre/blob/master/libdjvu/ddjvuapi.h)
  explicitly grants the later-version option. This permits use under GPLv3
  in the combined LLocr application; LLocr remains GPLv3.
- **Usage:** Required native decoding dependency of `DjVuDocument`, used by
  `DocumentModel` for DjVu input. Linked through `DjVuLibre::DjVuLibre`;
  library source/binaries are not bundled in this repository or downloaded
  by LLocr's CMake configuration.
- **Acquisition:** Unix development packages expose `ddjvuapi.pc`; Windows
  uses a separately supplied MSVC x64 DLL plus its `.lib` import library.
  No vcpkg port is assumed: the checked `djvulibre` and `libdjvu` port paths
  returned 404. See [build instructions](docs/06-dev-setup.md#djvulibre-required).
- **Copyright notices in the public API header:** Copyright (c) 2002 Leon
  Bottou and Yann Le Cun; Copyright (c) 2001 AT&T; derived from the DjVu
  Reference Library, Copyright (c) 1999–2001 LizardTech, Inc. Other source
  files carry additional contributor notices; retain the notices from the
  exact version distributed.
- **License text:** [`licenses/DjVuLibre-COPYING`](licenses/DjVuLibre-COPYING),
  from upstream `COPYING` (GPLv2 text; the later-version grant is in the
  source headers). LLocr's GPLv3 text is in [`LICENSE`](LICENSE).
- **Distribution:** Shipping a DLL does not avoid GPL obligations. Include
  the applicable license/copyright notices and fulfill the corresponding
  source obligations for the exact distributed library, including patches
  and build scripts. Also inventory and comply with licenses for any bundled
  transitive libraries (e.g. JPEG); Qt deployment tools alone do not complete
  native-library deployment or license compliance.
