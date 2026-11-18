# net68k — the Claude 68 TLS/HTTP engine, vendored, with the transport
# layer selectable.
#
# Everything above the socket (HttpClient, HttpResponse, Uri, RootCert,
# gason, http_parser, yuarel, micro-ecc, the mbedTLS 3.2.1 archives) is
# used unmodified; see UPSTREAM.md. The single difference in shape from
# upstream is that exactly one transport translation unit is compiled:
#
#   transport_ot.c      Open Transport   -DNET68K_TRANSPORT=ot      (default)
#   transport_mactcp.c  MacTCP           -DNET68K_TRANSPORT=mactcp
#
# Both implement the same symbols, so nothing above the socket knows
# which is present.

cmake_minimum_required(VERSION 3.20)

enable_language(ASM)

# Claude 68's CMakeLists.txt set this project-wide, and the MacTCP helper
# headers depend on it: TCPRoutines.h does `typedef int bool` in C, which
# a C23 compiler rejects because `bool` is a keyword there.
set(CMAKE_C_STANDARD 99)

set(NET68K_TRANSPORT "ot" CACHE STRING "Transport for net68k: ot or mactcp")
set_property(CACHE NET68K_TRANSPORT PROPERTY STRINGS ot mactcp)

set(NET68K_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if(NET68K_TRANSPORT STREQUAL "ot")
    set(NET68K_TRANSPORT_SRC net/transport_ot.c)
elseif(NET68K_TRANSPORT STREQUAL "mactcp")
    set(NET68K_TRANSPORT_SRC net/transport_mactcp.c)
else()
    message(FATAL_ERROR
        "NET68K_TRANSPORT must be ot or mactcp, not '${NET68K_TRANSPORT}'")
endif()

add_library(net68k STATIC
    net/HttpClient.cpp
    net/HttpResponse.cpp
    net/Uri.cpp
    net/RootCert.cpp
    net/gason.cpp
    net/http_parser.c
    net/yuarel.c
    net/uECC.c
    net/uecc_alt.c
    net/curve25519_sat.c
    net/vli_mult_comba_m68k.s
    net/vli_square_comba_m68k.s
    net/solinas_p256_m68k.s
    net/fast_alloc.c
    net/mac_entropy.c
    helpers/growzone.cpp
    helpers/prefs_io.cpp
    ${NET68K_TRANSPORT_SRC}
)

# Apple's Universal Interfaces, privately: MacTCP.h and the Open
# Transport headers exist only there, and they must not leak along the
# link graph into the application's Multiversal translation units.
#
# NET68K_UNIVERSAL_DIR is the "universal" directory produced by Retro68's
# interfaces-and-libraries.sh inside the toolchain prefix:
#
#   $NET68K_UNIVERSAL_DIR/CIncludes   Apple's prepared C headers
#   $NET68K_UNIVERSAL_DIR/lib68k      the 68k glue archives, OT included
#
set(NET68K_UNIVERSAL_DIR "" CACHE PATH
    "Retro68 'universal' staging directory (Apple's Universal Interfaces)")

if(NOT NET68K_UNIVERSAL_DIR)
    message(FATAL_ERROR
        "NET68K_UNIVERSAL_DIR is not set. Stage Apple's Universal Interfaces first:\n"
        "  interfaces-and-libraries.sh <toolchain> <Interfaces&Libraries> true false false\n"
        "  interfaces-and-libraries.sh <toolchain> --multiversal true false false\n"
        "then configure with -DNET68K_UNIVERSAL_DIR=<toolchain>/universal")
endif()

target_include_directories(net68k BEFORE PRIVATE
    ${NET68K_UNIVERSAL_DIR}/CIncludes
    ${CMAKE_CURRENT_SOURCE_DIR}/net
    ${CMAKE_CURRENT_SOURCE_DIR}/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/mactcp
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
)
target_include_directories(net68k PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/net
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/include
)
target_include_directories(net68k PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(net68k PUBLIC SSL_ENABLED)
target_compile_options(net68k PRIVATE
    -Os -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections)

# micro-ecc hot path flags, verbatim from Claude 68's CMakeLists.txt.
set(UECC_FLAGS "-m68020 -O3 \
 -Dasm_mult=1 \
 -Dasm_square=1 \
 -Dasm_solinas_p256=1 \
 -DuECC_OPTIMIZATION_LEVEL=2 \
 -DuECC_SQUARE_FUNC=1 \
 -DuECC_WORD_SIZE=4 \
 -DuECC_SUPPORTS_secp256r1=1 \
 -DuECC_SUPPORTS_secp160r1=0 \
 -DuECC_SUPPORTS_secp192r1=0 \
 -DuECC_SUPPORTS_secp224r1=0 \
 -DuECC_SUPPORTS_secp256k1=0 \
 -DuECC_VLI_NATIVE_LITTLE_ENDIAN=0")
set_source_files_properties(net/uECC.c net/uecc_alt.c
    PROPERTIES COMPILE_FLAGS "${UECC_FLAGS}")
# OPT #5/#6/#6b: looped Comba multiply, looped Comba square, and the
# Solinas P-256 column-sum reduction. 68020 only: MULU.L / MULS.L.
set_source_files_properties(
    net/vli_mult_comba_m68k.s
    net/vli_square_comba_m68k.s
    net/solinas_p256_m68k.s
    PROPERTIES COMPILE_FLAGS "-m68020")
# OPT #8b: saturated 8x32 X25519 reusing the looped Comba vli asm.
set_source_files_properties(net/curve25519_sat.c
    PROPERTIES COMPILE_FLAGS "-m68020 -O3 -Dsat_vli_asm=1")

# mbedTLS 3.2.1, built by Claude 68 for this toolchain. The .a files are
# plain m68k archives; if they ever fail to link, scripts/rebuild_mbedtls.sh
# in the upstream repo rebuilds them at the same version with the same
# flags. Do not substitute another version: the ALT hooks and the disabled
# modules are what makes the handshake fit inside Cloudflare's window.
#
# The whole set goes inside a linker group. mbedTLS's three archives refer
# to each other in both directions, and Apple's 68k Open Transport glue is
# split in two halves that do the same: libOpenTransport.a is the shared
# library's import stubs, which need InitOpenTransportCommon and
# __gOTClientRecord from libOpenTransportApp.a — while the app entry point
# that pulls libOpenTransportApp.a in is itself defined there. One
# left-to-right scan cannot resolve that; --start-group rescans until the
# symbol table stops changing.
set(NET68K_ARCHIVES
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/libmbedtls.a
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/libmbedx509.a
    ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/libmbedcrypto.a
)

if(NET68K_TRANSPORT STREQUAL "ot")
    set(NET68K_UNIV_LIB ${NET68K_UNIVERSAL_DIR}/lib68k)
    list(APPEND NET68K_ARCHIVES
        ${NET68K_UNIV_LIB}/libOpenTransportApp.a
        ${NET68K_UNIV_LIB}/libOpenTransport.a
        ${NET68K_UNIV_LIB}/libOpenTptInet.a
    )
else()
    list(APPEND NET68K_ARCHIVES
        ${CMAKE_CURRENT_SOURCE_DIR}/mactcp/libMacTCPHelper.a
    )
endif()

target_link_libraries(net68k PUBLIC
    -Wl,--start-group
    ${NET68K_ARCHIVES}
    -Wl,--end-group
)

