cmake_minimum_required(VERSION 3.16)
project(hil_protocol_core C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(hil_protocol STATIC
    src/hil_protocol.c
    src/hil_protocol_messages.c
    src/hil_protocol_reliability.c
    src/hil_transport.c
    src/hil_protocol_app.c
    src/hil_plant_model.c
    src/hil_plant_runtime.c
)
target_include_directories(hil_protocol PUBLIC include)
target_compile_options(hil_protocol PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

add_executable(test_hil_protocol
    tests/test_hil_protocol.c
)
target_link_libraries(test_hil_protocol PRIVATE hil_protocol)
target_compile_options(test_hil_protocol PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

enable_testing()
add_test(NAME hil_protocol_tests COMMAND test_hil_protocol)

add_executable(test_hil_protocol_messages
    tests/test_hil_protocol_messages.c
)
target_link_libraries(test_hil_protocol_messages PRIVATE hil_protocol)
target_compile_options(test_hil_protocol_messages PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME hil_protocol_message_tests COMMAND test_hil_protocol_messages)

add_executable(test_hil_protocol_reliability
    tests/test_hil_protocol_reliability.c
)
target_link_libraries(test_hil_protocol_reliability PRIVATE hil_protocol)
target_compile_options(test_hil_protocol_reliability PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)
add_test(NAME hil_protocol_reliability_tests COMMAND test_hil_protocol_reliability)

add_executable(test_hil_transport tests/test_hil_transport.c)
target_link_libraries(test_hil_transport PRIVATE hil_protocol)
target_compile_options(test_hil_transport PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME hil_transport_tests COMMAND test_hil_transport)

add_executable(test_hil_protocol_app tests/test_hil_protocol_app.c)
target_link_libraries(test_hil_protocol_app PRIVATE hil_protocol)
target_compile_options(test_hil_protocol_app PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME hil_protocol_app_tests COMMAND test_hil_protocol_app)

add_executable(test_hil_plant_model tests/test_hil_plant_model.c)
target_link_libraries(test_hil_plant_model PRIVATE hil_protocol)
target_compile_options(test_hil_plant_model PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME hil_plant_model_tests COMMAND test_hil_plant_model)

add_executable(test_hil_plant_runtime tests/test_hil_plant_runtime.c)
target_link_libraries(test_hil_plant_runtime PRIVATE hil_protocol)
target_compile_options(test_hil_plant_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME hil_plant_runtime_tests COMMAND test_hil_plant_runtime)

# DUT-only portable sensing and foreground integration; no HAL/vendor sources
# are added to the shared library or the Plant firmware.
set(DUT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../dut_stm32f411)
add_executable(test_dut_sensing
    ${DUT_SOURCE_DIR}/tests/test_dut_sensing.c
    ${DUT_SOURCE_DIR}/Src/dut_sensing.c
)
target_include_directories(test_dut_sensing PRIVATE ${DUT_SOURCE_DIR}/Inc)
target_compile_options(test_dut_sensing PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME dut_sensing_tests COMMAND test_dut_sensing)

add_executable(test_dut_runtime
    ${DUT_SOURCE_DIR}/tests/test_dut_runtime.c
    ${DUT_SOURCE_DIR}/Src/dut_runtime.c
    ${DUT_SOURCE_DIR}/Src/dut_sensing.c
)
target_include_directories(test_dut_runtime PRIVATE ${DUT_SOURCE_DIR}/tests/stubs ${DUT_SOURCE_DIR}/Inc)
target_link_libraries(test_dut_runtime PRIVATE hil_protocol)
target_compile_options(test_dut_runtime PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME dut_runtime_tests COMMAND test_dut_runtime)
