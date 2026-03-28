#include <catch2/catch_test_macros.hpp>
#include "filesystem_utils.h"

// --- is_image_file ---

TEST_CASE("is_image_file accepts supported image extensions", "[filesystem]") {
    REQUIRE(is_image_file("photo.jpg"));
    REQUIRE(is_image_file("photo.jpeg"));
    REQUIRE(is_image_file("photo.png"));
    REQUIRE(is_image_file("photo.bmp"));
    REQUIRE(is_image_file("photo.webp"));
}

TEST_CASE("is_image_file rejects non-image extensions", "[filesystem]") {
    REQUIRE_FALSE(is_image_file("document.pdf"));
    REQUIRE_FALSE(is_image_file("script.py"));
    REQUIRE_FALSE(is_image_file("readme.txt"));
    REQUIRE_FALSE(is_image_file("archive.zip"));
    REQUIRE_FALSE(is_image_file("model.param"));
}

TEST_CASE("is_image_file is case-insensitive", "[filesystem]") {
    REQUIRE(is_image_file("photo.JPG"));
    REQUIRE(is_image_file("photo.Png"));
    REQUIRE(is_image_file("photo.WEBP"));
    REQUIRE(is_image_file("photo.JpEg"));
}

TEST_CASE("is_image_file handles edge cases", "[filesystem]") {
    REQUIRE_FALSE(is_image_file(""));
    REQUIRE_FALSE(is_image_file("noextension"));
    REQUIRE_FALSE(is_image_file(".hidden"));
}

// --- get_file_extension ---

TEST_CASE("get_file_extension extracts extension correctly", "[filesystem]") {
    REQUIRE(get_file_extension("photo.jpg") == "jpg");
    REQUIRE(get_file_extension("path/to/photo.png") == "png");
    REQUIRE(get_file_extension("archive.tar.gz") == "gz");
}

TEST_CASE("get_file_extension returns empty for no extension", "[filesystem]") {
    REQUIRE(get_file_extension("noextension") == "");
    REQUIRE(get_file_extension("") == "");
}

// --- get_file_name_without_extension ---

TEST_CASE("get_file_name_without_extension strips extension", "[filesystem]") {
    REQUIRE(get_file_name_without_extension("photo.jpg") == "photo");
    REQUIRE(get_file_name_without_extension("archive.tar.gz") == "archive.tar");
}

TEST_CASE("get_file_name_without_extension returns full name when no extension", "[filesystem]") {
    REQUIRE(get_file_name_without_extension("noextension") == "noextension");
}
