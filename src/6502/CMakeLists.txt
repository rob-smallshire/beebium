# 6502 CPU emulator library
# Ported from B2 (https://github.com/tom-seddon/b2)
# Pure C library for maximum FFI portability

add_library(6502_lib STATIC
    src/6502.c
    include/6502/6502.h
    src/6502_internal.inl
)

# Include the generated .inl file from src/
target_include_directories(6502_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Public header
target_include_directories(6502_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Use C11 standard (inherited from root, but be explicit)
set_target_properties(6502_lib PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)
