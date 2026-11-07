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

