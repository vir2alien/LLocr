// One-time fixture authoring utility; not built or invoked by the test suite.
// Run from this directory with upstream cpaldjvu and djvm on PATH.
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;

static Bytes read(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error(path);
    return Bytes(std::istreambuf_iterator<char>(file), {});
}

static void write(const char* path, const Bytes& bytes)
{
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!file)
        throw std::runtime_error(path);
}

static void ppm(const char* path, int w, int h, const unsigned char colors[4][3])
{
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            file.write(reinterpret_cast<const char*>(colors[(y >= h / 2) * 2 + (x >= w / 2)]), 3);
    if (!file)
        throw std::runtime_error(path);
}

static void run(const char* command)
{
    if (std::system(command) != 0)
        throw std::runtime_error(command);
}

static void be32(Bytes& bytes, unsigned value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<unsigned char>(value >> shift));
}

static void infoOnly(const char* path, unsigned w, unsigned h)
{
    Bytes bytes = {'A','T','&','T','F','O','R','M'};
    be32(bytes, 22);
    bytes.insert(bytes.end(), {'D','J','V','U','I','N','F','O'});
    be32(bytes, 10);
    bytes.insert(bytes.end(), {static_cast<unsigned char>(w >> 8), static_cast<unsigned char>(w),
                              static_cast<unsigned char>(h >> 8), static_cast<unsigned char>(h),
                              24, 0, 100, 0, 22, 1});
    write(path, bytes);
}

int main()
{
    // Quadrant order: top-left, top-right, bottom-left, bottom-right.
    const unsigned char first[4][3] = {{255,0,0}, {0,255,0}, {0,0,255}, {255,255,255}};
    const unsigned char second[4][3] = {{0,255,255}, {255,0,255}, {255,255,0}, {0,0,0}};
    ppm("quadrants.ppm", 81, 57, first);
    ppm("second.ppm", 63, 45, second);
    run("cpaldjvu -colors 4 -dpi 100 quadrants.ppm quadrants.djvu");
    run("cpaldjvu -colors 4 -dpi 100 second.ppm second.djvu");
    Bytes rotated = read("quadrants.djvu");
    // INFO immediately follows FORM:DJVU. Flag 6 denotes 90 degrees CCW.
    if (std::string(rotated.begin() + 16, rotated.begin() + 20) != "INFO")
        throw std::runtime_error("Unexpected INFO location");
    rotated.at(33) = 6;
    write("rotated.djvu", rotated);
    run("djvm -c multipage.djvu quadrants.djvu rotated.djvu second.djvu");

    Bytes broken = read("multipage.djvu");
    unsigned found = 0;
    for (size_t i = 0; i + 18 <= broken.size(); ++i) {
        if (std::string(broken.begin() + i, broken.begin() + i + 4) == "INFO" && ++found == 2) {
            // Keep the bundle directory/chunk lengths intact; only page 2 has invalid geometry.
            broken[i + 8] = broken[i + 9] = 0;
            break;
        }
    }
    if (found != 2)
        throw std::runtime_error("Missing second INFO");
    write("bad-second-page.djvu", broken);

    // INFO-only documents exercise metadata bounding without allocating huge pixel buffers.
    infoOnly("wide-info.djvu", 24000, 1200);
    infoOnly("tall-info.djvu", 1200, 24000);
    infoOnly("large-info.djvu", 10000, 8000);
    std::remove("quadrants.ppm");
    std::remove("second.ppm");
    std::remove("second.djvu");
}
