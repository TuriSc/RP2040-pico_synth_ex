set(TARGET_NAME "pico_synth_ex_i2s")

if (NOT TARGET ${TARGET_NAME})
    add_library(${TARGET_NAME} INTERFACE)

    target_sources(${TARGET_NAME} INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/pico_synth_ex.c
            ${CMAKE_CURRENT_LIST_DIR}/sound_i2s/sound_i2s.c
    )

    pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/sound_i2s/sound_i2s_16bits.pio)

    target_include_directories(${TARGET_NAME} INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}
            ${CMAKE_CURRENT_LIST_DIR}/sound_i2s
    )

    target_link_libraries(${TARGET_NAME} INTERFACE
        pico_stdlib
        hardware_pwm
        hardware_irq
        hardware_pio
        hardware_dma
    )
endif()
