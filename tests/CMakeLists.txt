# ============================================================================
# Tests
#
#   gb_unit_tests - component-level unit tests (no ROMs required),
#                   registered with CTest, labeled "unit".
#   accuracy      - the ROM-downloading accuracy suite
#                   (tests/run_tests.sh), labeled "accuracy". Only
#                   registered when GB_DOWNLOAD_TESTS=ON so plain
#                   'ctest' stays offline.
# ============================================================================

add_executable(gb_unit_tests unit_tests.cpp)

target_link_libraries(gb_unit_tests PRIVATE
    gameboy_core
    gb_warnings
)

add_test(
    NAME unit_tests
    COMMAND gb_unit_tests
)
set_tests_properties(unit_tests PROPERTIES LABELS "unit")

if(GB_DOWNLOAD_TESTS)
    find_program(GB_TIMEOUT timeout REQUIRED)
    add_test(
        NAME accuracy
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_tests.sh $<TARGET_FILE:gameboy>
    )
    set_tests_properties(accuracy PROPERTIES LABELS "accuracy")
endif()
