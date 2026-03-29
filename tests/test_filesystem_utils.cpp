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

// --- path_is_directory ---

TEST_CASE("path_is_directory returns true for an existing directory", "[filesystem]") {
    REQUIRE(path_is_directory("/tmp"));
}

TEST_CASE("path_is_directory returns false for non-existent path", "[filesystem]") {
    REQUIRE_FALSE(path_is_directory("/tmp/upscayl_nonexistent_dir_xyzzy"));
}

TEST_CASE("path_is_directory returns false for a file path", "[filesystem]") {
    // /dev/null is always present and is not a directory
    REQUIRE_FALSE(path_is_directory("/dev/null"));
}

// --- filepath_is_readable ---

TEST_CASE("filepath_is_readable returns true for a readable file", "[filesystem]") {
    REQUIRE(filepath_is_readable("/dev/null"));
}

TEST_CASE("filepath_is_readable returns false for non-existent file", "[filesystem]") {
    REQUIRE_FALSE(filepath_is_readable("/tmp/upscayl_nonexistent_file_xyzzy.bin"));
}

// --- list_directory ---

TEST_CASE("list_directory succeeds on an accessible directory", "[filesystem]") {
    std::vector<path_t> files;
    REQUIRE(list_directory("/tmp", files) == 0);
}

TEST_CASE("list_directory returns error for non-existent path", "[filesystem]") {
    std::vector<path_t> files;
    REQUIRE(list_directory("/tmp/upscayl_nonexistent_dir_xyzzy", files) != 0);
}

TEST_CASE("list_directory clears output vector on failure", "[filesystem]") {
    std::vector<path_t> files = {"leftover.png"};
    list_directory("/tmp/upscayl_nonexistent_dir_xyzzy", files);
    REQUIRE(files.empty());
}

TEST_CASE("list_directory returns error when path is a file, not a directory", "[filesystem]") {
    std::vector<path_t> files;
    REQUIRE(list_directory("/dev/null", files) != 0);
}

// --- sanitize_filepath ---

TEST_CASE("sanitize_filepath returns path unchanged when file is readable", "[filesystem]") {
    REQUIRE(sanitize_filepath("/dev/null") == "/dev/null");
}

TEST_CASE("sanitize_filepath falls back to executable directory for missing file", "[filesystem]") {
    const std::string missing = "upscayl_nonexistent_model_xyzzy.param";
    path_t result = sanitize_filepath(missing);
    REQUIRE(result == get_executable_directory() + missing);
}
