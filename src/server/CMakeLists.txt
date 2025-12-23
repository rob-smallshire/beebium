# beebium-server executable

add_executable(beebium-server
    main.cpp
)

target_link_libraries(beebium-server
    PRIVATE
        beebium_service
)
