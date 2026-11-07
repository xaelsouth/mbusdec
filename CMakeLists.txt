cmake_minimum_required(VERSION 3.16)
project(mbusdec VERSION 0.1.0 LANGUAGES C)

include(GNUInstallDirs)

set(MBUS_AES_BACKEND "bundled" CACHE STRING "AES backend: bundled or openssl")
set_property(CACHE MBUS_AES_BACKEND PROPERTY STRINGS bundled openssl)
add_library(mbus STATIC src/mbus.c src/mbus_records.c src/mbus_profile.c
    src/mbus_security.c src/mbus_manufacturer.c)
target_include_directories(mbus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/private)
target_compile_features(mbus PUBLIC c_std_11)
target_compile_definitions(mbus PRIVATE MBUS_BUILD_VERSION="${PROJECT_VERSION}")
if(MBUS_AES_BACKEND STREQUAL "bundled")
    target_sources(mbus PRIVATE thirdparty/tiny-aes-c/aes.c)
    target_include_directories(mbus PRIVATE thirdparty/tiny-aes-c)
    target_compile_definitions(mbus PRIVATE MBUS_AES_BUNDLED=1 CBC=1 ECB=1 CTR=0)
elseif(MBUS_AES_BACKEND STREQUAL "openssl")
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    target_link_libraries(mbus PUBLIC OpenSSL::Crypto)
    target_compile_definitions(mbus PRIVATE MBUS_AES_OPENSSL=1)
else()
    message(FATAL_ERROR "MBUS_AES_BACKEND must be bundled or openssl")
endif()
set_target_properties(mbus PROPERTIES C_EXTENSIONS OFF)
if(NOT MSVC)
    target_link_libraries(mbus PUBLIC m)
    target_compile_options(mbus PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(mbus_cli STATIC src/cli_json_output.c src/cli_keys.c thirdparty/mjson/mjson.c)
target_include_directories(mbus_cli PRIVATE include/private thirdparty/mjson)
target_compile_definitions(mbus_cli PRIVATE
    MJSON_ENABLE_RPC=0 MJSON_ENABLE_BASE64=0 MJSON_ENABLE_MERGE=0
    MJSON_ENABLE_PRETTY=0 MJSON_ENABLE_NEXT=0)
target_link_libraries(mbus_cli PUBLIC mbus)
set_target_properties(mbus_cli PROPERTIES C_EXTENSIONS OFF)
if(NOT MSVC)
    target_compile_options(mbus_cli PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(mbusdec src/main.c)
target_include_directories(mbusdec PRIVATE include/private)
target_link_libraries(mbusdec PRIVATE mbus_cli)
if(NOT MSVC)
    target_compile_options(mbusdec PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_mbus tests/test_mbus.c)
    target_link_libraries(test_mbus PRIVATE mbus)
    target_compile_definitions(test_mbus PRIVATE MBUS_EXPECTED_VERSION="${PROJECT_VERSION}")
    add_test(NAME library COMMAND test_mbus)

    add_executable(test_cli_keys tests/test_cli_keys.c)
    target_include_directories(test_cli_keys PRIVATE include/private)
    target_link_libraries(test_cli_keys PRIVATE mbus_cli)
    add_test(NAME key_selection COMMAND test_cli_keys)

    add_executable(test_library_example examples/library_decode.c)
    target_link_libraries(test_library_example PRIVATE mbus)
    add_test(NAME library_example COMMAND test_library_example)
    set_tests_properties(library_example PROPERTIES
        PASS_REGULAR_EXPRESSION "energy: 123.4 Wh")

    add_executable(test_manufacturer_example examples/manufacturer_decode.c)
    target_link_libraries(test_manufacturer_example PRIVATE mbus)
    add_test(NAME manufacturer_example COMMAND test_manufacturer_example)

    find_program(SH_EXECUTABLE sh)
    find_program(JQ_EXECUTABLE jq)
    if(SH_EXECUTABLE AND JQ_EXECUTABLE)
    add_executable(test_fixture_decoder tests/fixture_decoder.c tests/fixture_json_input.c)
    target_include_directories(test_fixture_decoder PRIVATE include/private thirdparty/mjson)
    target_link_libraries(test_fixture_decoder PRIVATE mbus_cli)
    add_test(NAME fixture_decoder COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.sh
             $<TARGET_FILE:test_fixture_decoder> ${JQ_EXECUTABLE})
    add_test(NAME real_telegrams COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_real_telegrams.sh
             $<TARGET_FILE:test_fixture_decoder> ${JQ_EXECUTABLE})
    add_test(NAME libmbus_telegrams COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_libmbus.sh
             $<TARGET_FILE:test_fixture_decoder> ${JQ_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/libmbus)
    add_test(NAME upstream_aes COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_upstream_aes.sh
             $<TARGET_FILE:test_fixture_decoder> ${JQ_EXECUTABLE})
    add_test(NAME examples COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_examples.sh
             $<TARGET_FILE:test_fixture_decoder> ${JQ_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    add_test(NAME rtl_wmbus COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_rtl_wmbus.sh
             $<TARGET_FILE:mbusdec> ${JQ_EXECUTABLE})
    add_test(NAME filter COMMAND ${SH_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_filter.sh
             $<TARGET_FILE:mbusdec> ${JQ_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/examples)
    else()
        message(STATUS "Shell and jq not found; JSON CLI tests are unavailable")
    endif()

    install(TARGETS mbus mbusdec
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES include/mbus.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mbusdec)
    install(FILES thirdparty/mjson/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mbusdec
            RENAME mjson-LICENSE)
    if(MBUS_AES_BACKEND STREQUAL "bundled")
        install(FILES thirdparty/tiny-aes-c/unlicense.txt
                DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/mbusdec RENAME tiny-aes-c-LICENSE)
    endif()

    set(CPACK_GENERATOR TGZ)
    include(CPack)
endif()
