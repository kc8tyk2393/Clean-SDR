cmake_minimum_required(VERSION 3.21)

set(WDSP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(FFTW_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../fftw64")
set(BRICK2_FFTW_DLL "${FFTW_ROOT}/libfftw3-3.dll" CACHE FILEPATH "FFTW double-precision DLL" FORCE)

file(GLOB WDSP_SOURCES CONFIGURE_DEPENDS "${WDSP_ROOT}/*.c")

add_library(wdsp SHARED ${WDSP_SOURCES})
target_compile_definitions(wdsp PRIVATE
    WDSP_BUILD
    _CRT_SECURE_NO_WARNINGS
    _WIN32_WINNT=0x0601
)
target_include_directories(wdsp
    PUBLIC "${WDSP_ROOT}"
    PRIVATE "${FFTW_ROOT}"
)
target_link_libraries(wdsp PRIVATE
    "${FFTW_ROOT}/libfftw3-3.dll.a"
    avrt
)
set_target_properties(wdsp PROPERTIES
    C_STANDARD 11
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    PREFIX ""
)

foreach(heavy IN ITEMS nnr_model_0.c nnr_model_1.c calculus.c)
    set_source_files_properties("${WDSP_ROOT}/${heavy}" PROPERTIES COMPILE_OPTIONS "-O0")
endforeach()

function(brick2_copy_wdsp_runtime dest)
    add_custom_command(TARGET ${dest} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:wdsp>"
            "$<TARGET_FILE_DIR:${dest}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BRICK2_FFTW_DLL}"
            "$<TARGET_FILE_DIR:${dest}>"
    )
endfunction()
