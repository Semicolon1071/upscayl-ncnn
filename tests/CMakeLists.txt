include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(upscayl-tests
    test_filesystem_utils.cpp
    test_filesystem_extended.cpp
    test_image_utils.cpp
    ${CMAKE_SOURCE_DIR}/src/image_utils.cpp
)

target_include_directories(upscayl-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

target_link_libraries(upscayl-tests PRIVATE Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(upscayl-tests)

# --- Codec tests (need libwebp, STB headers) ---
add_executable(upscayl-codec-tests
    test_codecs.cpp
    test_codecs_stb.cpp
)

target_include_directories(upscayl-codec-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/src/libwebp/src
)

target_compile_definitions(upscayl-codec-tests PRIVATE
    TEST_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/tests/fixtures"
)

target_link_libraries(upscayl-codec-tests PRIVATE Catch2::Catch2WithMain webp)

catch_discover_tests(upscayl-codec-tests)
