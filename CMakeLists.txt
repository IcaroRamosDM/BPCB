cmake_minimum_required(VERSION 3.20)
project(bancada_pcb VERSION 0.16.0 LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MINGW)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)
endif()
option(ENABLE_SANITIZERS "ASan e UBSan nos testes de desenvolvimento" OFF)
if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined,float-cast-overflow -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined,float-cast-overflow)
endif()
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
pkg_check_modules(ARCHIVE REQUIRED IMPORTED_TARGET libarchive)
pkg_check_modules(CRYPTO REQUIRED IMPORTED_TARGET libcrypto)
pkg_check_modules(USB REQUIRED IMPORTED_TARGET libusb-1.0)
pkg_check_modules(POPPLER REQUIRED IMPORTED_TARGET poppler-glib)
add_library(
    bancada_core STATIC
    core/communication/compatibility.c
    core/communication/firmware_bundle.c
    core/wiring.c
    core/communication/frame.c
    core/communication/external_process.c
    core/communication/firmware.c
    core/communication/lab.c
    core/communication/peripherals.c
    core/communication/uart_protocol.c
    core/communication/replay.c
    core/communication/qualification.c
    core/communication/tools.c
    core/communication/openocd.c
    core/communication/config.c
    core/communication/ch347.c
    core/communication/protocol.c
    core/communication/usb.c
    core/communication/hid.c
    core/communication/serial.c
    core/communication/session.c
    core/plot.c
    core/diagnostic.c
    core/reading.c
    core/preparation.c
    core/backup.c
    core/report.c
    core/rigol.c
    core/history.c
    core/calibration.c
    core/pdf.c
    core/assessment.c
    core/common.c
    core/model.c
    core/packages.c
    core/jobs.c
    core/browse.c
)
if(WIN32)
    target_sources(bancada_core PRIVATE core/windows_compat.c)
    target_compile_definitions(bancada_core PUBLIC _WIN32_WINNT=0x0601)
    target_link_libraries(bancada_core PUBLIC ws2_32 shell32 hid setupapi uuid advapi32)
endif()
target_include_directories(bancada_core PUBLIC core)
target_link_libraries(
    bancada_core PUBLIC PkgConfig::JSONC PkgConfig::ARCHIVE PkgConfig::CRYPTO PkgConfig::POPPLER
                        PkgConfig::USB Threads::Threads m
)
target_compile_options(bancada_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
add_executable(bancada-core core/cli.c)
target_link_libraries(bancada-core PRIVATE bancada_core)
add_executable(demo_worker workers/demo.c)
target_link_libraries(demo_worker PRIVATE bancada_core)
add_executable(communication_worker workers/communication.c)
target_link_libraries(communication_worker PRIVATE bancada_core)
foreach(exe bancada-core demo_worker communication_worker)
    target_compile_options(${exe} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endforeach()
if(NOT WIN32)
    set(DEMO_STAGE ${CMAKE_BINARY_DIR}/package-stage/demo)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/Demonstracao-V1.bancada
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/packages/demo ${DEMO_STAGE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEMO_STAGE}/sources
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/workers/demo.c
                ${CMAKE_SOURCE_DIR}/workers/worker_api.h ${DEMO_STAGE}/sources
        COMMAND ${CMAKE_COMMAND} "-DMANIFEST=${DEMO_STAGE}/manifest.json" -P
                ${CMAKE_SOURCE_DIR}/cmake/NativeManifest.cmake
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:demo_worker> ${DEMO_STAGE}/code/demo_worker
        COMMAND bancada-core pack ${DEMO_STAGE} ${CMAKE_BINARY_DIR}/Demonstracao-V1.bancada
        DEPENDS bancada-core demo_worker packages/demo/manifest.json cmake/NativeManifest.cmake
        VERBATIM
    )
    add_custom_target(bancadas ALL DEPENDS ${CMAKE_BINARY_DIR}/Demonstracao-V1.bancada)
else()
    add_custom_target(bancadas)
endif()
# Exemplos nativos independentes, com o mesmo executor de comunicação API 1.
foreach(bus i2c spi uart jtag)
    string(TOUPPER ${bus} BUS_TITLE)
    set(COMM_STAGE ${CMAKE_BINARY_DIR}/package-stage/communication-${bus})
    file(COPY packages/communication-${bus} DESTINATION ${CMAKE_BINARY_DIR}/package-stage)
    if(NOT WIN32)
        add_custom_command(
            OUTPUT ${CMAKE_BINARY_DIR}/Comunicacao-${BUS_TITLE}.bancada
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${CMAKE_SOURCE_DIR}/packages/communication-${bus} ${COMM_STAGE}
            COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/core
                    ${COMM_STAGE}/sources/core
            COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/workers/communication.c
                    ${CMAKE_SOURCE_DIR}/workers/worker_api.h ${COMM_STAGE}/sources/
            COMMAND ${CMAKE_COMMAND} "-DMANIFEST=${COMM_STAGE}/manifest.json" -P
                    ${CMAKE_SOURCE_DIR}/cmake/NativeManifest.cmake
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:communication_worker>
                    ${COMM_STAGE}/code/communication_worker
            COMMAND bancada-core pack ${COMM_STAGE}
                    ${CMAKE_BINARY_DIR}/Comunicacao-${BUS_TITLE}.bancada
            DEPENDS bancada-core communication_worker packages/communication-${bus}/manifest.json
                    packages/communication-${bus}/LEIA-ME.txt
            VERBATIM
        )
        add_custom_target(
            bancada_communication_${bus} ALL
            DEPENDS ${CMAKE_BINARY_DIR}/Comunicacao-${BUS_TITLE}.bancada
        )
        add_dependencies(bancadas bancada_communication_${bus})
    endif()
endforeach()
option(BUILD_GUI "Interface FLTK em C++" ON)
if(BUILD_GUI)
    find_package(FLTK 1.4 CONFIG REQUIRED)
    if(NOT WIN32)
        pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
    endif()
    add_executable(bancada_pcb ui/main.cpp)
    target_link_libraries(bancada_pcb PRIVATE bancada_core fltk::fltk fltk::images ${CMAKE_DL_LIBS})
    if(NOT WIN32)
        target_link_libraries(bancada_pcb PRIVATE PkgConfig::FONTCONFIG)
    endif()
    target_compile_options(bancada_pcb PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    add_dependencies(bancada_pcb bancadas)
endif()
include(CTest)
if(BUILD_TESTING AND NOT WIN32)
    function(bpcb_test target source name)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE bancada_core)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        add_test(NAME ${name} COMMAND ${target} ${ARGN})
        set_tests_properties(${name} PROPERTIES TIMEOUT 60 RESOURCE_LOCK bpcb_bench)
    endfunction()
    bpcb_test(communication_usb_test tests/communication_usb_test.c communication_usb_offline)
    bpcb_test(communication_ch347_hid_io_test tests/communication_ch347_hid_io_test.c
              communication_ch347_hid_io_offline
    )
    bpcb_test(communication_ch347_test tests/communication_ch347_test.c communication_ch347_offline)
    bpcb_test(communication_test tests/communication_test.c communication_protocol_offline)
    bpcb_test(history_test tests/history_test.c history_offline ${CMAKE_BINARY_DIR})
    bpcb_test(continuity_test tests/continuity_test.c continuity_offline ${CMAKE_BINARY_DIR})
    bpcb_test(communication_tools_test tests/communication_tools_test.c communication_tools_offline)
    bpcb_test(openocd_test tests/openocd_test.c communication_openocd_offline)
    bpcb_test(communication_terminal_test tests/communication_terminal_test.c
              communication_terminal_offline
    )
    bpcb_test(communication_lab_test tests/lab_test.c communication_lab_offline)
    bpcb_test(communication_lab_pool_test tests/lab_pool_test.c communication_lab_pool_offline)
    bpcb_test(external_process_test tests/process_portable_test.c external_process_offline)
    bpcb_test(capture_buffer_test tests/capture_buffer_test.cpp capture_buffer_offline)
    bpcb_test(communication_lab_boundaries_test tests/lab_boundaries_test.c
              communication_lab_boundaries_offline
    )
    bpcb_test(workbench_test tests/workbench_test.c workbench_offline)
    target_link_options(continuity_test PRIVATE -Wl,--wrap=bc_write_json)
    target_link_options(openocd_test PRIVATE -Wl,--wrap=bc_comm_list)
    target_link_options(
        communication_lab_pool_test PRIVATE -Wl,--wrap=bc_comm_open -Wl,--wrap=bc_comm_close
        -Wl,--wrap=bc_comm_transaction
    )
    add_executable(communication_sequence_test_runner tests/communication_sequence_test_runner.c)
    target_link_libraries(communication_sequence_test_runner PRIVATE bancada_core)
    add_executable(firmware_plan_test_runner tests/firmware_plan_runner.c)
    target_link_libraries(firmware_plan_test_runner PRIVATE bancada_core)
    target_link_options(firmware_plan_test_runner PRIVATE -Wl,--wrap=bc_openocd_run)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME communication_uart_offline
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/communication_serial_test.py
                     $<TARGET_FILE:bancada-core>
    )
    add_test(
        NAME communication_integration_offline
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/communication_integration_test.py
                $<TARGET_FILE:bancada-core> ${CMAKE_BINARY_DIR} $<TARGET_FILE:communication_worker>
    )
    add_test(NAME rigol_offline
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/rigol_test.py
                     $<TARGET_FILE:bancada-core>
    )
    add_test(NAME firmware_manager_offline
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/firmware_test.py
                     $<TARGET_FILE:bancada-core> $<TARGET_FILE:firmware_plan_test_runner>
    )
    add_test(NAME firmware_bundle_adversarial
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/firmware_bundle_test.py
                     $<TARGET_FILE:bancada-core>
    )
    if(BUILD_GUI)
        find_program(XVFB_RUN xvfb-run)
        find_program(XDOTOOL xdotool)
        if(XVFB_RUN AND XDOTOOL)
            add_test(NAME interface_offline COMMAND bash ${CMAKE_SOURCE_DIR}/tests/ui_test.sh
                                                    ${CMAKE_BINARY_DIR}
            )
            set_tests_properties(interface_offline PROPERTIES TIMEOUT 90 RESOURCE_LOCK bpcb_bench)
        endif()
    endif()
endif()
if(ENABLE_SANITIZERS AND NOT WIN32)
    pkg_check_modules(FONTCONFIG_SANITIZER REQUIRED IMPORTED_TARGET fontconfig)
    foreach(exe bancada-core continuity_test)
        if(TARGET ${exe})
            target_sources(${exe} PRIVATE tests/sanitizer_cleanup.c)
            target_link_libraries(${exe} PRIVATE PkgConfig::FONTCONFIG_SANITIZER)
        endif()
    endforeach()
endif()
if(WIN32)
    enable_language(RC)
    foreach(exe bancada-core demo_worker communication_worker)
        target_sources(${exe} PRIVATE packaging/windows.rc)
        target_include_directories(${exe} PRIVATE packaging)
    endforeach()
    if(BUILD_GUI)
        target_sources(bancada_pcb PRIVATE packaging/windows.rc)
        target_include_directories(bancada_pcb PRIVATE packaging)
        set_target_properties(bancada_pcb PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
endif()
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_custom_target(
        verificar-formato
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/formatar.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Verificando o estilo dos fontes ativos"
        VERBATIM
    )
endif()
