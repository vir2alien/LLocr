# Synthetic DjVu test fixtures

These images were authored for LLocr tests, not copied from upstream samples.
The seven `.djvu` files total 1,256 bytes. Tests read these committed files;
no encoder, Python, external executable, or network access is needed at runtime.

| File | Contents |
| --- | --- |
| `quadrants.djvu` | 81 × 57 pixels, 100 DPI; red/green above blue/white |
| `rotated.djvu` | Same encoded pixels, INFO orientation flag 6 (90° counter-clockwise); displayed size 57 × 81, green/white above red/blue |
| `multipage.djvu` | Three bundled pages: quadrants, rotated quadrants, then a 63 × 45 cyan/magenta above yellow/black image |
| `bad-second-page.djvu` | Copy of the bundle with only page 2's INFO width set to zero; directory offsets, chunk lengths and the first page are preserved |
| `wide-info.djvu` | INFO-only metadata, 24000 × 1200 |
| `tall-info.djvu` | INFO-only metadata, 1200 × 24000 |
| `large-info.djvu` | INFO-only metadata, 10000 × 8000 |

The last three are metadata-boundary fixtures, not complete encoded images;
tests query their page sizes without rendering huge buffers. The damaged
bundle exercises failure after a valid first page, so atomic append tests
can detect accidentally retaining partially appended pages.

## Reproduction and provenance

`generate.cpp` is the complete original fixture recipe. It writes two PPM
images, invokes upstream `cpaldjvu -colors 4 -dpi 100`, sets the INFO rotation
flag, bundles pages with `djvm -c`, and writes the metadata/error fixtures.
Odd image widths intentionally exercise RGB888 scanline padding. Color
assertions sample quadrant interiors with a small codec tolerance.

Generation used the locally installed **DjVuLibre 3.5.30** source archive:

- URL: https://downloads.sourceforge.net/djvu/djvulibre-3.5.30.tar.gz
- Archive SHA256: `ee5e457d4cfebe566f94b99e5e3d3cc7f5c79ddb741c2ac2ba2e456f00329644`
- Encoder sources: `tools/cpaldjvu.cpp`, `tools/jb2tune.cpp`,
  `tools/jb2cmp/{classify,cuts,frames,patterns}.cpp`, and `tools/djvm.cpp`.
- No upstream source or sample content is redistributed in this directory.

On the documented Windows development machine, run `generate-msvc.bat` from
this directory to rebuild the authoring tools against the local Release
DjVuLibre library and regenerate fixtures. The script removes its temporary
objects/executables after success. On other platforms compile `generate.cpp`
with a C++17 compiler and run it here with `cpaldjvu` and `djvm` on PATH.
Regeneration is optional and is not part of CMake or CTest.

Missing/empty/plain-text inputs, Unicode paths, raster PNGs and a two-page
PDF are created inside `QTemporaryDir` by the tests themselves.
