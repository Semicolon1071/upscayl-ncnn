#include <catch2/catch_test_macros.hpp>
#include "image_utils.h"
#include <cstdlib>
#include <cstring>

// --- parse_int_array ---

TEST_CASE("parse_int_array parses single value", "[parsing]") {
    auto result = parse_int_array("42");
    REQUIRE(result.size() == 1);
    REQUIRE(result[0] == 42);
}

TEST_CASE("parse_int_array parses comma-separated values", "[parsing]") {
    auto result = parse_int_array("100,200,300");
    REQUIRE(result.size() == 3);
    REQUIRE(result[0] == 100);
    REQUIRE(result[1] == 200);
    REQUIRE(result[2] == 300);
}

TEST_CASE("parse_int_array parses two values", "[parsing]") {
    auto result = parse_int_array("1,2");
    REQUIRE(result.size() == 2);
    REQUIRE(result[0] == 1);
    REQUIRE(result[1] == 2);
}

// --- parse_resize_spec ---

TEST_CASE("parse_resize_spec parses WxH format", "[parsing]") {
    int w, h, mode;
    REQUIRE(parse_resize_spec("1920x1080", &w, &h, &mode));
    REQUIRE(w == 1920);
    REQUIRE(h == 1080);
    REQUIRE(mode == 0); // default
}

TEST_CASE("parse_resize_spec parses WxH:mode format", "[parsing]") {
    int w, h, mode;
    REQUIRE(parse_resize_spec("1920x1080:catmullrom", &w, &h, &mode));
    REQUIRE(w == 1920);
    REQUIRE(h == 1080);
    REQUIRE(mode == 4); // catmullrom is index 4
}

TEST_CASE("parse_resize_spec rejects invalid mode", "[parsing]") {
    int w, h, mode;
    REQUIRE_FALSE(parse_resize_spec("1920x1080:invalidmode", &w, &h, &mode));
}

TEST_CASE("parse_resize_spec width-only mode", "[parsing]") {
    int w, h, mode;
    REQUIRE(parse_resize_spec("1920", &w, &h, &mode, true));
    REQUIRE(w == 1920);
}

TEST_CASE("parse_resize_spec all filter modes", "[parsing]") {
    int w, h, mode;
    const char *modes[] = {"default", "box", "triangle", "cubicbspline",
                           "catmullrom", "mitchell", "pointsample"};
    for (int i = 0; i < 7; i++) {
        std::string input = "100x100:" + std::string(modes[i]);
        REQUIRE(parse_resize_spec(input.c_str(), &w, &h, &mode));
        REQUIRE(mode == i);
    }
}

// --- detect_scale_from_model_name ---

TEST_CASE("detect_scale_from_model_name finds x4", "[scale]") {
    REQUIRE(detect_scale_from_model_name("realesrgan-x4plus") == 4);
}

TEST_CASE("detect_scale_from_model_name finds 2x", "[scale]") {
    REQUIRE(detect_scale_from_model_name("model-2x") == 2);
}

TEST_CASE("detect_scale_from_model_name finds x1", "[scale]") {
    REQUIRE(detect_scale_from_model_name("model-x1-fast") == 1);
}

TEST_CASE("detect_scale_from_model_name finds x8", "[scale]") {
    REQUIRE(detect_scale_from_model_name("super-x8-model") == 8);
}

TEST_CASE("detect_scale_from_model_name finds x16", "[scale]") {
    REQUIRE(detect_scale_from_model_name("model-16x") == 16);
    REQUIRE(detect_scale_from_model_name("model-x16") == 16);
}

TEST_CASE("detect_scale_from_model_name returns 0 for unknown", "[scale]") {
    REQUIRE(detect_scale_from_model_name("somemodel") == 0);
    REQUIRE(detect_scale_from_model_name("") == 0);
}

// --- calculate_proportional_height ---

TEST_CASE("proportional height basic", "[dimensions]") {
    REQUIRE(calculate_proportional_height(100, 200, 50) == 100);
    REQUIRE(calculate_proportional_height(1920, 1080, 960) == 540);
}

TEST_CASE("proportional height identity", "[dimensions]") {
    REQUIRE(calculate_proportional_height(1920, 1080, 1920) == 1080);
}

TEST_CASE("proportional height rounds down", "[dimensions]") {
    // 2 * 2 / 3 = 1.33 -> 1
    REQUIRE(calculate_proportional_height(3, 2, 2) == 1);
}

// --- calculate_tile_count ---

TEST_CASE("tile count exact division", "[tiles]") {
    REQUIRE(calculate_tile_count(400, 100) == 4);
}

TEST_CASE("tile count rounds up", "[tiles]") {
    REQUIRE(calculate_tile_count(401, 100) == 5);
    REQUIRE(calculate_tile_count(1, 100) == 1);
}

// --- validate_and_round_compression ---

TEST_CASE("compression rounding", "[compression]") {
    REQUIRE(validate_and_round_compression(0) == 0.0f);
    REQUIRE(validate_and_round_compression(33) == 30.0f);
    REQUIRE(validate_and_round_compression(37) == 40.0f);
    REQUIRE(validate_and_round_compression(100) == 100.0f);
    REQUIRE(validate_and_round_compression(55) == 60.0f);
}

TEST_CASE("compression out of range", "[compression]") {
    REQUIRE(validate_and_round_compression(-1) == -1.0f);
    REQUIRE(validate_and_round_compression(101) == -1.0f);
    REQUIRE(validate_and_round_compression(-0.1f) == -1.0f);
}

// --- grayscale_to_rgb ---

TEST_CASE("grayscale to rgb conversion", "[channels]") {
    unsigned char gray[] = {128, 255, 0};
    unsigned char *rgb = grayscale_to_rgb(gray, 3, 1);
    REQUIRE(rgb != nullptr);
    // pixel 0: all 128
    REQUIRE(rgb[0] == 128);
    REQUIRE(rgb[1] == 128);
    REQUIRE(rgb[2] == 128);
    // pixel 1: all 255
    REQUIRE(rgb[3] == 255);
    REQUIRE(rgb[4] == 255);
    REQUIRE(rgb[5] == 255);
    // pixel 2: all 0
    REQUIRE(rgb[6] == 0);
    REQUIRE(rgb[7] == 0);
    REQUIRE(rgb[8] == 0);
    free(rgb);
}

TEST_CASE("grayscale to rgb 2x2 image", "[channels]") {
    unsigned char gray[] = {10, 20, 30, 40};
    unsigned char *rgb = grayscale_to_rgb(gray, 2, 2);
    REQUIRE(rgb != nullptr);
    for (int i = 0; i < 4; i++) {
        REQUIRE(rgb[i * 3 + 0] == gray[i]);
        REQUIRE(rgb[i * 3 + 1] == gray[i]);
        REQUIRE(rgb[i * 3 + 2] == gray[i]);
    }
    free(rgb);
}

// --- grayscale_alpha_to_rgba ---

TEST_CASE("grayscale alpha to rgba conversion", "[channels]") {
    unsigned char ga[] = {128, 200, 255, 100};
    unsigned char *rgba = grayscale_alpha_to_rgba(ga, 2, 1);
    REQUIRE(rgba != nullptr);
    // pixel 0: gray=128, alpha=200
    REQUIRE(rgba[0] == 128);
    REQUIRE(rgba[1] == 128);
    REQUIRE(rgba[2] == 128);
    REQUIRE(rgba[3] == 200);
    // pixel 1: gray=255, alpha=100
    REQUIRE(rgba[4] == 255);
    REQUIRE(rgba[5] == 255);
    REQUIRE(rgba[6] == 255);
    REQUIRE(rgba[7] == 100);
    free(rgba);
}
