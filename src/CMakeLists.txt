macro(compile_shader SHADER_SRC)
    set(SHADER_SRC_FULLPATH ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER_SRC})

    get_filename_component(SHADER_SRC_NAME_WE ${SHADER_SRC} NAME_WE)
    set(SHADER_SPV_HEX_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_SRC_NAME_WE}.spv.hex.h)
    add_custom_command(
        OUTPUT ${SHADER_SPV_HEX_FILE}
        COMMAND ${GLSLANGVALIDATOR_EXECUTABLE}
        ARGS -V -s -x -o ${SHADER_SPV_HEX_FILE} ${SHADER_SRC_FULLPATH}
        DEPENDS ${SHADER_SRC_FULLPATH}
        COMMENT "Building SPIR-V module ${SHADER_SRC_NAME_WE}.spv"
        VERBATIM
    )
    set_source_files_properties(${SHADER_SPV_HEX_FILE} PROPERTIES GENERATED TRUE)
    list(APPEND SHADER_SPV_HEX_FILES ${SHADER_SPV_HEX_FILE})

    # fp16 storage
    set(SHADER_fp16s_SRC_NAME_WE "${SHADER_SRC_NAME_WE}_fp16s")

    set(SHADER_fp16s_SPV_HEX_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_fp16s_SRC_NAME_WE}.spv.hex.h)
    add_custom_command(
        OUTPUT ${SHADER_fp16s_SPV_HEX_FILE}
        COMMAND ${GLSLANGVALIDATOR_EXECUTABLE}
        ARGS -DNCNN_fp16_storage=1 -V -s -x -o ${SHADER_fp16s_SPV_HEX_FILE} ${SHADER_SRC_FULLPATH}
        DEPENDS ${SHADER_SRC_FULLPATH}
        COMMENT "Building SPIR-V module ${SHADER_fp16s_SRC_NAME_WE}.spv"
        VERBATIM
    )
    set_source_files_properties(${SHADER_fp16s_SPV_HEX_FILE} PROPERTIES GENERATED TRUE)
    list(APPEND SHADER_SPV_HEX_FILES ${SHADER_fp16s_SPV_HEX_FILE})

    # int8 storage
    set(SHADER_int8s_SRC_NAME_WE "${SHADER_SRC_NAME_WE}_int8s")

    set(SHADER_int8s_SPV_HEX_FILE ${CMAKE_CURRENT_BINARY_DIR}/${SHADER_int8s_SRC_NAME_WE}.spv.hex.h)
    add_custom_command(
        OUTPUT ${SHADER_int8s_SPV_HEX_FILE}
        COMMAND ${GLSLANGVALIDATOR_EXECUTABLE}
        ARGS -DNCNN_fp16_storage=1 -DNCNN_int8_storage=1 -V -s -x -o ${SHADER_int8s_SPV_HEX_FILE} ${SHADER_SRC_FULLPATH}
        DEPENDS ${SHADER_SRC_FULLPATH}
        COMMENT "Building SPIR-V module ${SHADER_int8s_SRC_NAME_WE}.spv"
        VERBATIM
    )
    set_source_files_properties(${SHADER_int8s_SPV_HEX_FILE} PROPERTIES GENERATED TRUE)
    list(APPEND SHADER_SPV_HEX_FILES ${SHADER_int8s_SPV_HEX_FILE})
endmacro()

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${THIRD_PARTY_DIR})

# look for vulkan compute shader and compile
set(SHADER_SPV_HEX_FILES)

compile_shader(realesrgan_preproc.comp)
compile_shader(realesrgan_postproc.comp)
compile_shader(realesrgan_preproc_tta.comp)
compile_shader(realesrgan_postproc_tta.comp)

add_custom_target(generate-spirv DEPENDS ${SHADER_SPV_HEX_FILES})

add_executable(upscayl-bin main.cpp realesrgan.cpp)

add_dependencies(upscayl-bin generate-spirv)

set(REALESRGAN_LINK_LIBRARIES ncnn webp ${Vulkan_LIBRARY})

if(USE_STATIC_MOLTENVK)
    find_library(CoreFoundation NAMES CoreFoundation)
    find_library(Foundation NAMES Foundation)
    find_library(Metal NAMES Metal)
    find_library(QuartzCore NAMES QuartzCore)
    find_library(CoreGraphics NAMES CoreGraphics)
    find_library(Cocoa NAMES Cocoa)
    find_library(IOKit NAMES IOKit)
    find_library(IOSurface NAMES IOSurface)

    list(APPEND REALESRGAN_LINK_LIBRARIES
        ${Metal}
        ${QuartzCore}
        ${CoreGraphics}
        ${Cocoa}
        ${IOKit}
        ${IOSurface}
        ${Foundation}
        ${CoreFoundation}
    )
endif()

if(OPENMP_FOUND)
    list(APPEND REALESRGAN_LINK_LIBRARIES ${OpenMP_CXX_LIBRARIES})
endif()

target_link_libraries(upscayl-bin ${REALESRGAN_LINK_LIBRARIES} -static-libstdc++)
