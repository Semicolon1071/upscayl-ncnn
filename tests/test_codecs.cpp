#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_STDIO
#include "stb_image.h"
#include "stb_image_write.h"
#include "stb_image_resize2.h"
#include "webp_image.h"

namespace fs = std::filesystem;

// Helper: read entire file into malloc'd buffer
static unsigned char *read_file(const char *path, int *len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return nullptr;
    fseek(fp, 0, SEEK_END);
    *len = (int)ftell(fp);
    rewind(fp);
    unsigned char *buf = (unsigned char *)malloc(*len);
    if (buf)
        fread(buf, 1, *len, fp);
    fclose(fp);
    return buf;
}

static std::string fixture_path(const char *name)
{
    return std::string(TEST_FIXTURES_DIR) + "/" + name;
}

// ============================================================
// STB Image decode tests
// ============================================================

TEST_CASE("STB PNG load returns correct dimensions", "[codec][stb]")
{
    int len;
    unsigned char *buf = read_file(fixture_path("rgb_4x4.png").c_str(), &len);
    REQUIRE(buf != nullptr);

    int w, h, c;
    unsigned char *pixels = stbi_load_from_memory(buf, len, &w, &h, &c, 0);
    REQUIRE(pixels != nullptr);
    REQUIRE(w == 4);
    REQUIRE(h == 4);
    REQUIRE(c == 3);
    stbi_image_free(pixels);
    free(buf);
}

TEST_CASE("STB PNG load RGBA returns alpha channel", "[codec][stb]")
{
    int len;
    unsigned char *buf = read_file(fixture_path("rgba_4x4.png").c_str(), &len);
    REQUIRE(buf != nullptr);

    int w, h, c;
    unsigned char *pixels = stbi_load_from_memory(buf, len, &w, &h, &c, 0);
    REQUIRE(pixels != nullptr);
    REQUIRE(w == 4);
    REQUIRE(h == 4);
    REQUIRE(c == 4);
    stbi_image_free(pixels);
    free(buf);
}

TEST_CASE("STB PNG write then read lossless round-trip", "[codec][stb]")
{
    const int W = 4, H = 4, C = 3;
    unsigned char original[W * H * C];
    for (int i = 0; i < W * H * C; i++)
        original[i] = (unsigned char)(i % 256);

    auto tmp = fs::temp_directory_path() / "stb_roundtrip_test.png";
    int ok = stbi_write_png(tmp.c_str(), W, H, C, original, W * C);
    REQUIRE(ok != 0);

    int len;
    unsigned char *buf = read_file(tmp.c_str(), &len);
    REQUIRE(buf != nullptr);

    int w2, h2, c2;
    unsigned char *reloaded = stbi_load_from_memory(buf, len, &w2, &h2, &c2, 0);
    REQUIRE(reloaded != nullptr);
    REQUIRE(w2 == W);
    REQUIRE(h2 == H);
    REQUIRE(c2 == C);
    REQUIRE(memcmp(original, reloaded, W * H * C) == 0);

    stbi_image_free(reloaded);
    free(buf);
    fs::remove(tmp);
}

TEST_CASE("STB JPG write then read is within tolerance", "[codec][stb]")
{
    const int W = 4, H = 4, C = 3;
    unsigned char original[W * H * C];
    for (int i = 0; i < W * H * C; i++)
        original[i] = 128; // uniform gray for stable JPEG

    auto tmp = fs::temp_directory_path() / "stb_roundtrip_test.jpg";
    int ok = stbi_write_jpg(tmp.c_str(), W, H, C, original, 100);
    REQUIRE(ok != 0);

    int len;
    unsigned char *buf = read_file(tmp.c_str(), &len);
    REQUIRE(buf != nullptr);

    int w2, h2, c2;
    unsigned char *reloaded = stbi_load_from_memory(buf, len, &w2, &h2, &c2, 0);
    REQUIRE(reloaded != nullptr);
    REQUIRE(w2 == W);
    REQUIRE(h2 == H);

    for (int i = 0; i < W * H * C; i++)
    {
        REQUIRE(abs((int)original[i] - (int)reloaded[i]) <= 5);
    }

    stbi_image_free(reloaded);
    free(buf);
    fs::remove(tmp);
}

TEST_CASE("STB resize produces correct dimensions", "[codec][stb]")
{
    const int W = 4, H = 4, C = 3;
    unsigned char input[W * H * C];
    memset(input, 128, sizeof(input));

    const int OUT_W = 8, OUT_H = 8;
    unsigned char output[OUT_W * OUT_H * C];

    stbir_resize_uint8_srgb(input, W, H, 0, output, OUT_W, OUT_H, 0,
                            static_cast<stbir_pixel_layout>(C));

    // Verify output is non-zero (upscaled from uniform gray)
    bool all_zero = true;
    for (int i = 0; i < OUT_W * OUT_H * C; i++)
    {
        if (output[i] != 0)
        {
            all_zero = false;
            break;
        }
    }
    REQUIRE_FALSE(all_zero);
}

// ============================================================
// WebP codec tests
// ============================================================

TEST_CASE("WebP decode from fixture", "[codec][webp]")
{
    int len;
    unsigned char *buf = read_file(fixture_path("rgb_4x4.webp").c_str(), &len);
    REQUIRE(buf != nullptr);

    int w, h, c;
    unsigned char *pixels = webp_load(buf, len, &w, &h, &c);
    REQUIRE(pixels != nullptr);
    REQUIRE(w == 4);
    REQUIRE(h == 4);
    REQUIRE(c == 3);

    free(pixels);
    free(buf);
}

TEST_CASE("WebP lossless encode then decode round-trip", "[codec][webp]")
{
    const int W = 4, H = 4, C = 3;
    unsigned char original[W * H * C];
    for (int i = 0; i < W * H * C; i++)
        original[i] = (unsigned char)(i % 256);

    auto tmp = fs::temp_directory_path() / "webp_roundtrip_test.webp";
    int ok = webp_save(tmp.c_str(), W, H, C, original, 100); // quality>=100 is lossless
    REQUIRE(ok == 1);

    int len;
    unsigned char *buf = read_file(tmp.c_str(), &len);
    REQUIRE(buf != nullptr);

    int w2, h2, c2;
    unsigned char *reloaded = webp_load(buf, len, &w2, &h2, &c2);
    REQUIRE(reloaded != nullptr);
    REQUIRE(w2 == W);
    REQUIRE(h2 == H);
    REQUIRE(c2 == C);
    REQUIRE(memcmp(original, reloaded, W * H * C) == 0);

    free(reloaded);
    free(buf);
    fs::remove(tmp);
}

TEST_CASE("WebP lossy encode then decode within tolerance", "[codec][webp]")
{
    const int W = 4, H = 4, C = 3;
    unsigned char original[W * H * C];
    for (int i = 0; i < W * H * C; i++)
        original[i] = 128;

    auto tmp = fs::temp_directory_path() / "webp_lossy_roundtrip.webp";
    int ok = webp_save(tmp.c_str(), W, H, C, original, 90);
    REQUIRE(ok == 1);

    int len;
    unsigned char *buf = read_file(tmp.c_str(), &len);
    REQUIRE(buf != nullptr);

    int w2, h2, c2;
    unsigned char *reloaded = webp_load(buf, len, &w2, &h2, &c2);
    REQUIRE(reloaded != nullptr);

    for (int i = 0; i < W * H * C; i++)
    {
        REQUIRE(abs((int)original[i] - (int)reloaded[i]) <= 10);
    }

    free(reloaded);
    free(buf);
    fs::remove(tmp);
}

TEST_CASE("WebP RGBA encode then decode round-trip", "[codec][webp]")
{
    const int W = 4, H = 4, C = 4;
    unsigned char original[W * H * C];
    for (int i = 0; i < W * H; i++)
    {
        original[i * 4 + 0] = (unsigned char)(i * 10);       // R
        original[i * 4 + 1] = (unsigned char)(i * 10 + 1);   // G
        original[i * 4 + 2] = (unsigned char)(i * 10 + 2);   // B
        original[i * 4 + 3] = (unsigned char)(128 + i * 5);  // A (non-trivial so WebP preserves alpha)
    }

    auto tmp = fs::temp_directory_path() / "webp_rgba_roundtrip.webp";
    int ok = webp_save(tmp.c_str(), W, H, C, original, 100);
    REQUIRE(ok == 1);

    int len;
    unsigned char *buf = read_file(tmp.c_str(), &len);
    REQUIRE(buf != nullptr);

    int w2, h2, c2;
    unsigned char *reloaded = webp_load(buf, len, &w2, &h2, &c2);
    REQUIRE(reloaded != nullptr);
    REQUIRE(w2 == W);
    REQUIRE(h2 == H);
    REQUIRE(c2 == C);
    REQUIRE(memcmp(original, reloaded, W * H * C) == 0);

    free(reloaded);
    free(buf);
    fs::remove(tmp);
}
