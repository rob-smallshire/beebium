# beebium server executables - one per machine type

# Common include directory for server templates
set(SERVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Function to create a beebium server executable for a specific machine type
function(add_beebium_server TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})

    target_include_directories(${TARGET_NAME}
        PRIVATE
            ${SERVER_INCLUDE_DIR}
    )

    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            BEEBIUM_VERSION="${PROJECT_VERSION}"
    )

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            beebium_service
    )
endfunction()

# Model-specific executables
add_beebium_server(beebium-model-b main_model_b.cpp)
add_beebium_server(beebium-model-b-plus main_model_b_plus.cpp)

# Copy ROMs to build output directory for development
set(ROM_OUTPUT_DIR "${CMAKE_BINARY_DIR}/roms")
file(MAKE_DIRECTORY ${ROM_OUTPUT_DIR})

add_custom_command(TARGET beebium-model-b POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BEEBIUM_ROM_DIR}/acorn-mos_1_20.rom"
        "${BEEBIUM_ROM_DIR}/acorn-mos_2_0.rom"
        "${BEEBIUM_ROM_DIR}/bbc-basic_2.rom"
        "${ROM_OUTPUT_DIR}/"
    COMMENT "Copying ROMs to build directory"
)

# Install rules
install(TARGETS beebium-model-b beebium-model-b-plus
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${BEEBIUM_ROM_DIR}/
    DESTINATION share/beebium/roms
    FILES_MATCHING PATTERN "*.rom"
)
