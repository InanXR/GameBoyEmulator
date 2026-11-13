# Compiler warning flags shared by all first-party targets.
#
# Exposed as an INTERFACE target; link it and you get the flags:
#     target_link_libraries(my_target PRIVATE gb_warnings)

add_library(gb_warnings INTERFACE)

if(MSVC)
    target_compile_options(gb_warnings INTERFACE /W4)
else()
    target_compile_options(gb_warnings INTERFACE
        -Wall
        -Wextra          # sensible extra warnings
        -Wshadow         # variable shadowing
        -Wnon-virtual-dtor
        -Wold-style-cast  # C-style casts are banned in the codebase
    )
endif()
