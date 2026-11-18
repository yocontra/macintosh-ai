# Third-party notices

This application ships, or links against, the following third-party components.

## Vendored in `libs/net68k/`

### Claude 68 TLS/HTTP engine

- Source: <https://github.com/SLKK-design/Claude68>, commit
  `41258f2801cceab2a1ec9b30f13e8adcfd27abf1`
- License: 0BSD — Copyright (C) 2022-2026 Lin van der Slikke
- Modifications: the MacTCP transport (`net/transport_mactcp.c`) is kept
  unmodified, and a parallel Open Transport transport (`net/transport_ot.c`)
  has been added. Everything above the socket layer is used as published.
- Full text: `libs/net68k/THIRD_PARTY_LICENSES`

### Mbed TLS 3.2.1

- Source: <https://github.com/Mbed-TLS/mbedtls>, tag `v3.2.1`
- License: Apache License 2.0 — Copyright The Mbed TLS Contributors
- Shipped as prebuilt archives: `libs/net68k/mbedtls/libmbedcrypto.a`,
  `libmbedx509.a`, `libmbedtls.a`, plus headers. The archives were built for
  this toolchain with modules and options selected for 68k (see
  `libs/net68k/CMakeLists.txt`); the source is unmodified.
- License text: <https://www.apache.org/licenses/LICENSE-2.0>

  This product includes software developed by the Mbed TLS Contributors.

### micro-ecc

- Source: <https://github.com/kmackay/micro-ecc>
- License: BSD 2-Clause — Copyright 2014 Kenneth MacKay
- Files: `libs/net68k/net/uECC.c`, `uECC.h`, `uECC_vli.h`, `uecc_alt.c`, and
  the 68020 assembly helpers (`vli_mult_comba_m68k.s`,
  `vli_square_comba_m68k.s`, `solinas_p256_m68k.s`, `curve25519_sat.c`).
- Modifications: `uecc_alt.c` provides the 68k-specific optimisation hooks
  used by the engine.

### gason

- Source: <https://github.com/vivkin/gason>
- License: MIT — Copyright Ivan Vashchaev
- Files: `libs/net68k/net/gason.cpp`, `gason.hpp`

### http_parser

- Source: <https://github.com/nodejs/http-parser>
- License: MIT — Copyright Joyent, Inc. and other Node contributors
- Files: `libs/net68k/net/http_parser.c`, `http_parser.h`

### yuarel

- Source: <https://github.com/jacke/yuarel>
- License: MIT — Copyright (C) 2016, 2017 Jack Engqvist Johansson
- Files: `libs/net68k/net/yuarel.c`, `yuarel.h`

### MacTCPHelper

- Source: <https://github.com/antscode/MacTCPHelper>
- License: no license file was published in the upstream repository at the
  time of vendoring; it is used only by the optional MacTCP transport.
- Files: `libs/net68k/mactcp/` (headers derived from Apple's MacTCP API, plus
  `libMacTCPHelper.a`).

## Build-time only

- [Retro68](https://github.com/autc04/Retro68) (GPL-3.0) provides the
  cross-compiler and the application packaging tools. It is not distributed
  with the application.
- Apple's Universal Interfaces & Libraries are used at build time for the
  Open Transport headers and glue libraries. They are not part of this
  repository or of the released binaries.
