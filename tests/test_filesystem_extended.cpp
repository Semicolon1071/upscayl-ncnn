#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include "filesystem_utils.h"

namespace fs = std::filesystem;

// --- path_is_directory ---

TEST_CASE("path_is_directory returns true for directories", "[filesystem]") {
    auto tmp = fs::temp_directory_path() / "upscayl_test_dir";
    fs::create_directories(tmp);
    REQUIRE(path_is_directory(tmp.string()));
    fs::remove(tmp);
}

TEST_CASE("path_is_directory returns false for files", "[filesystem]") {
    auto tmp = fs::temp_directory_path() / "upscayl_test_file.txt";
    { std::ofstream f(tmp); f << "test"; }
    REQUIRE_FALSE(path_is_directory(tmp.string()));
    fs::remove(tmp);
}

TEST_CASE("path_is_directory returns false for nonexistent path", "[filesystem]") {
    REQUIRE_FALSE(path_is_directory("/nonexistent/path/zzz"));
}

// --- list_directory ---

TEST_CASE("list_directory finds image files", "[filesystem]") {
    auto tmp = fs::temp_directory_path() / "upscayl_test_listdir";
    fs::create_directories(tmp);

    { std::ofstream f(tmp / "a.png"); f << "fake"; }
    { std::ofstream f(tmp / "b.jpg"); f << "fake"; }
    { std::ofstream f(tmp / "c.txt"); f << "fake"; }

    std::vector<path_t> images;
    int rc = list_directory(tmp.string(), images);
    REQUIRE(rc == 0);
    REQUIRE(images.size() == 2);

    // Results should be sorted
    REQUIRE(images[0] == "a.png");
    REQUIRE(images[1] == "b.jpg");

    fs::remove_all(tmp);
}

TEST_CASE("list_directory returns empty for directory with no images", "[filesystem]") {
    auto tmp = fs::temp_directory_path() / "upscayl_test_noimg";
    fs::create_directories(tmp);

    { std::ofstream f(tmp / "readme.txt"); f << "text"; }
    { std::ofstream f(tmp / "data.csv"); f << "data"; }

    std::vector<path_t> images;
    int rc = list_directory(tmp.string(), images);
    REQUIRE(rc == 0);
    REQUIRE(images.empty());

    fs::remove_all(tmp);
}

TEST_CASE("list_directory returns error for nonexistent directory", "[filesystem]") {
    std::vector<path_t> images;
    int rc = list_directory("/nonexistent/zzz", images);
    REQUIRE(rc == -1);
}

// --- filepath_is_readable ---

TEST_CASE("filepath_is_readable returns true for readable file", "[filesystem]") {
    auto tmp = fs::temp_directory_path() / "upscayl_readable_test.txt";
    { std::ofstream f(tmp); f << "test"; }
    REQUIRE(filepath_is_readable(tmp.string()));
    fs::remove(tmp);
}

TEST_CASE("filepath_is_readable returns false for nonexistent file", "[filesystem]") {
    REQUIRE_FALSE(filepath_is_readable("/nonexistent/zzz.txt"));
}

// --- get_executable_directory ---

TEST_CASE("get_executable_directory returns non-empty path", "[filesystem]") {
    path_t dir = get_executable_directory();
    REQUIRE_FALSE(dir.empty());
    // Should end with a slash
    REQUIRE(dir.back() == '/');
}
