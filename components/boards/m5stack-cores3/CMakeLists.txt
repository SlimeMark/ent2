idf_component_register(
    SRCS "board.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "."
    PRIV_REQUIRES "board_utils" "dac"
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)
