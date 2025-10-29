cmake_minimum_required(VERSION 3.21.0)

###########################################################################
#                                                                         #
#   libtropic ESP-IDF Component                                           #
#                                                                         #
###########################################################################

# Options for the component
option(LT_BUILD_EXAMPLES "Compile example code as part of libtropic library" OFF)
option(LT_BUILD_TESTS "Compile functional tests' code as part of libtropic library" OFF)
option(LT_HELPERS "Compile helper function" ON)
option(LT_USE_INT_PIN "Use INT pin instead of polling for TROPIC01's response" OFF)
option(LT_SEPARATE_L3_BUFF "Define L3 buffer separately out of the handle" OFF)
option(LT_PRINT_SPI_DATA "Print SPI communication to console, used to debug low level communication" OFF)
option(LT_STRICT_COMP_FLAGS "Enable strict compilation flags for libtropic" OFF)
option(LT_ASAN "Enable AddressSanitizer (ASan)" OFF)

# TROPIC01 silicon revision
set(LT_SILICON_REV "ACAB" CACHE STRING "Set TROPIC01 silicon revision")
set_property(CACHE LT_SILICON_REV PROPERTY STRINGS "ABAB" "ACAB")

# TROPIC01 FW version
set(LT_CPU_FW_VERSION "1_0_1" CACHE STRING "Set TROPIC01 FW version")
set_property(CACHE LT_CPU_FW_VERSION PROPERTY STRINGS "1_0_0" "1_0_1")

# Validate FW version
get_property(lt_cpu_fw_version_choices CACHE LT_CPU_FW_VERSION PROPERTY STRINGS)
set(cpu_fw_valid FALSE)
foreach(cpu_fw_choice IN LISTS lt_cpu_fw_version_choices)
    if(LT_CPU_FW_VERSION STREQUAL ${cpu_fw_choice})
        set(cpu_fw_valid TRUE)
        break()
    endif()
endforeach()
if (NOT cpu_fw_valid)
    message(FATAL_ERROR "Invalid TROPIC01 FW version: '${LT_CPU_FW_VERSION}'\nAvailable FW versions: ${lt_cpu_fw_version_choices}")
endif()

# Crypto provider
set(LT_CRYPTO "trezor_crypto" CACHE STRING "Set a cryptography provider")
set_property(CACHE LT_CRYPTO PROPERTY STRINGS "trezor_crypto")

# Logging options
set(LT_LOG_LVL "" CACHE STRING "Set log level")
set_property(CACHE LT_LOG_LVL PROPERTY STRINGS "None" "Error" "Warning" "Info" "Debug")

# Use INFO logging level when building tests/examples
if(LT_BUILD_TESTS OR LT_BUILD_EXAMPLES)
    if(NOT LT_LOG_LVL)
        message(STATUS "No logging level specified, defaulting to INFO for tests/examples.")
        set(LT_LOG_LVL "Info" CACHE BOOL "Set log level to INFO." FORCE)
    endif()
endif()

# Internal logging macros (default: all logging disabled)
set(LT_LOG_ENABLE_INFO  0 CACHE INTERNAL "")
set(LT_LOG_ENABLE_WARN  0 CACHE INTERNAL "")
set(LT_LOG_ENABLE_ERROR 0 CACHE INTERNAL "")
set(LT_LOG_ENABLE_DEBUG 0 CACHE INTERNAL "")

# Set logging ENABLE flags based on selected logging level
if(LT_LOG_LVL STREQUAL "Debug")
    message(STATUS "Log level set to DEBUG")
    set(LT_LOG_ENABLE_DEBUG 1)
    set(LT_LOG_ENABLE_INFO  1)
    set(LT_LOG_ENABLE_WARN  1)
    set(LT_LOG_ENABLE_ERROR 1)
elseif(LT_LOG_LVL STREQUAL "Info")
    message(STATUS "Log level set to INFO")
    set(LT_LOG_ENABLE_DEBUG 0)
    set(LT_LOG_ENABLE_INFO  1)
    set(LT_LOG_ENABLE_WARN  1)
    set(LT_LOG_ENABLE_ERROR 1)
elseif(LT_LOG_LVL STREQUAL "Warning")
    message(STATUS "Log level set to WARNING")
    set(LT_LOG_ENABLE_DEBUG 0)
    set(LT_LOG_ENABLE_INFO  0)
    set(LT_LOG_ENABLE_WARN  1)
    set(LT_LOG_ENABLE_ERROR 1)
elseif(LT_LOG_LVL STREQUAL "Error")
    message(STATUS "Log level set to ERROR")
    set(LT_LOG_ENABLE_DEBUG 0)
    set(LT_LOG_ENABLE_INFO  0)
    set(LT_LOG_ENABLE_WARN  0)
    set(LT_LOG_ENABLE_ERROR 1)
elseif(LT_LOG_LVL STREQUAL "None")
    message(STATUS "Log level set to NONE (logging is disabled)")
elseif(NOT LT_LOG_LVL)
    message(STATUS "No logging level specified, defaulting to NONE (logging is disabled)")
else()
    get_property(lt_log_lvl_choices CACHE LT_LOG_LVL PROPERTY STRINGS)
    message(FATAL_ERROR "Incorrect logging level specified: '${LT_LOG_LVL}'\nAvailable logging levels: ${lt_log_lvl_choices}")
endif()

###########################################################################
# Collect source files                                                    #
###########################################################################

set(LIBTROPIC_SRCS
    # Crypto sources
    hal/crypto/trezor_crypto/lt_crypto_trezor_aesgcm.c
    hal/crypto/trezor_crypto/lt_crypto_trezor_ed25519.c
    hal/crypto/trezor_crypto/lt_crypto_trezor_ecdsa.c
    hal/crypto/trezor_crypto/lt_crypto_trezor_sha256.c
    hal/crypto/trezor_crypto/lt_crypto_trezor_hmac_sha256.c
    hal/crypto/trezor_crypto/lt_crypto_trezor_x25519.c
    
    # Core libtropic functions
    keys/keys.c
    src/libtropic.c
    src/lt_crc16.c
    src/lt_l1_port_wrap.c
    src/lt_l1.c
    src/libtropic_l2.c
    src/lt_l2_frame_check.c
    src/lt_l3_process.c
    src/libtropic_l3.c
    src/lt_hkdf.c
    src/lt_random.c
    src/lt_asn1_der.c
)

set(LIBTROPIC_INCLUDE_DIRS
    include
    src
)

# Add FW update files based on silicon revision
if (LT_SILICON_REV STREQUAL "ABAB")
    set(BL_FOLDER boot_v_1_0_1)
elseif (LT_SILICON_REV STREQUAL "ACAB")
    set(BL_FOLDER boot_v_2_0_1)
else()
    get_property(lt_silicon_rev_choices CACHE LT_SILICON_REV PROPERTY STRINGS)    
    message(FATAL_ERROR "Invalid silicon revision: '${LT_SILICON_REV}'\nAvailable silicon revisions: ${lt_silicon_rev_choices}")
endif()

list(APPEND LIBTROPIC_INCLUDE_DIRS
    TROPIC01_fw_update_files/${BL_FOLDER}/fw_v_${LT_CPU_FW_VERSION}
)

# Add example sources if enabled
if(LT_BUILD_EXAMPLES)
    list(APPEND LIBTROPIC_SRCS
        examples/lt_ex_fw_update.c
        examples/lt_ex_show_chip_id_and_fwver.c
        examples/lt_ex_hello_world_separate_API.c
        examples/lt_ex_hello_world.c
        examples/lt_ex_hw_wallet.c
        examples/lt_ex_macandd.c
    )
    list(APPEND LIBTROPIC_INCLUDE_DIRS examples)
endif()

# Add test sources if enabled
if(LT_BUILD_TESTS)
    list(APPEND LIBTROPIC_SRCS
        tests/functional/lt_test_common.c
        tests/functional/lt_test_rev_ecdsa_sign.c
        tests/functional/lt_test_rev_eddsa_sign.c
        tests/functional/lt_test_ire_pairing_key_slots.c
        tests/functional/lt_test_ire_write_i_config.c
        tests/functional/lt_test_rev_ping.c
        tests/functional/lt_test_rev_r_mem.c
        tests/functional/lt_test_rev_erase_r_config.c
        tests/functional/lt_test_rev_handshake_req.c
        tests/functional/lt_test_rev_mcounter.c
        tests/functional/lt_test_rev_get_info_req_app.c
        tests/functional/lt_test_rev_get_info_req_bootloader.c
        tests/functional/lt_test_rev_read_i_config.c
        tests/functional/lt_test_rev_read_r_config.c
        tests/functional/lt_test_rev_resend_req.c
        tests/functional/lt_test_rev_sleep_req.c
        tests/functional/lt_test_rev_startup_req.c
        tests/functional/lt_test_rev_write_r_config.c
        tests/functional/lt_test_rev_ecc_key_generate.c
        tests/functional/lt_test_rev_ecc_key_store.c
        tests/functional/lt_test_rev_random_value_get.c
        tests/functional/lt_test_rev_mac_and_destroy.c
        tests/functional/lt_test_rev_get_log_req.c
    )
    list(APPEND LIBTROPIC_INCLUDE_DIRS tests/functional)
endif()

# Handle key generation for examples/tests
if (LT_BUILD_EXAMPLES OR LT_BUILD_TESTS)
    set(DEFAULT_LAB_BATCH_PKG_DIR "2025-06-27T07-51-29Z__prod_C2S_T200__provisioning__lab_batch_package/")
    set(LT_SH0_PRIV_PATH_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/provisioning_data/${DEFAULT_LAB_BATCH_PKG_DIR}sh0_key_pair/sh_x25519_private_key_2025-03-24T09-15-15Z.pem")
    set(LT_SH0_PRIV_PATH "${LT_SH0_PRIV_PATH_DEFAULT}" CACHE FILEPATH "Path to file with SH0 private key in PEM or DER format")

    if (LT_SH0_PRIV_PATH STREQUAL "")
        message(FATAL_ERROR "LT_SH0_PRIV_PATH must be set when LT_BUILD_EXAMPLES or LT_BUILD_TESTS is enabled.")
    endif()

    set(SH0_PRIV_RAW_FILE "${CMAKE_BINARY_DIR}/sh0_priv.bin")
    set(SH0_PUB_RAW_FILE "${CMAKE_BINARY_DIR}/sh0_pub.bin")
    set(KEYS_C_FILE "${CMAKE_BINARY_DIR}/sh0_keys.c")
    set(EXTRACT_KEY_DATA_PYTHON_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/scripts/extract_x25519_key_data.py")

    add_custom_command(
        OUTPUT ${SH0_PRIV_RAW_FILE} ${SH0_PUB_RAW_FILE}
        COMMAND python3 ${EXTRACT_KEY_DATA_PYTHON_SCRIPT} --priv-key ${LT_SH0_PRIV_PATH} --priv-out ${SH0_PRIV_RAW_FILE} --pub-out ${SH0_PUB_RAW_FILE}
        DEPENDS "${EXTRACT_KEY_DATA_PYTHON_SCRIPT}" "${LT_SH0_PRIV_PATH}"
        COMMENT "Extracting raw data from X25519 private key"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${KEYS_C_FILE}"
        COMMAND ${CMAKE_COMMAND}
                -DSH0_PRIV_RAW_PATH=${SH0_PRIV_RAW_FILE}
                -DSH0_PUB_RAW_PATH=${SH0_PUB_RAW_FILE}
                -DOUTPUT_C_FILE=${KEYS_C_FILE}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/generate_sh0_keys.cmake"
        DEPENDS "${SH0_PRIV_RAW_FILE}" "${SH0_PUB_RAW_FILE}"
        COMMENT "Generating sh0_keys.c from DER files"
        VERBATIM
    )

    list(APPEND LIBTROPIC_SRCS "${KEYS_C_FILE}")
endif()

###########################################################################
# Register ESP-IDF Component                                              #
###########################################################################

idf_component_register(
    SRCS ${LIBTROPIC_SRCS}
    INCLUDE_DIRS ${LIBTROPIC_INCLUDE_DIRS}
    REQUIRES driver esp_system
)

# Add trezor-crypto as a subdirectory and link it
add_subdirectory(vendor/trezor_crypto/ "trezor_crypto")
target_compile_definitions(trezor_crypto PRIVATE AES_VAR USE_INSECURE_PRNG)
target_link_libraries(${COMPONENT_LIB} PRIVATE trezor_crypto)

# Add compile options
target_compile_options(${COMPONENT_LIB} PRIVATE -ffunction-sections -fdata-sections)
target_link_options(${COMPONENT_LIB} INTERFACE -Wl,--gc-sections)

# Add compile definitions
target_compile_definitions(${COMPONENT_LIB} PRIVATE "$<$<CONFIG:DEBUG>:LT_REDUNDANT_ARG_CHECK>")

# Propagate logging settings
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    LT_LOG_ENABLE_DEBUG=${LT_LOG_ENABLE_DEBUG}
    LT_LOG_ENABLE_ERROR=${LT_LOG_ENABLE_ERROR}
    LT_LOG_ENABLE_WARN=${LT_LOG_ENABLE_WARN}
    LT_LOG_ENABLE_INFO=${LT_LOG_ENABLE_INFO}
)

# Silicon revision definitions
if (LT_SILICON_REV STREQUAL "ABAB")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ABAB)
elseif (LT_SILICON_REV STREQUAL "ACAB")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ACAB)
endif()

# Other options
if(LT_PRINT_SPI_DATA)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LT_PRINT_SPI_DATA)
endif()

if(LT_HELPERS)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC LT_HELPERS)
endif()

if(LT_USE_INT_PIN)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LT_USE_INT_PIN)
endif()

if(LT_SEPARATE_L3_BUFF)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE LT_SEPARATE_L3_BUFF)
endif()

# Crypto provider definitions
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    LT_CRYPTO_TREZOR=1
    LT_CRYPTO_MBEDTLS=0
)

# Apply strict compilation flags if needed
if(LT_STRICT_COMP_FLAGS OR LT_BUILD_EXAMPLES OR LT_BUILD_TESTS)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wpedantic
        -Wshadow
        -Wformat=2
        -Wcast-align
        -Wfloat-equal
        -Wnull-dereference
        -Wredundant-decls
        -Wlogical-op
        -Wstrict-prototypes
        -Wunused-result
        -Wmissing-prototypes
        -fstack-protector-strong
    )
endif()
