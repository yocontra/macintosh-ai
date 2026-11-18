# net68k — vendored TLS/HTTP engine

This directory is the network engine from **Claude 68**
(`https://github.com/SLKK-design/Claude68`), vendored at commit
`41258f2801cceab2a1ec9b30f13e8adcfd27abf1` (2026-10-30, "Fix gason toBool"),
plus a new Open Transport transport. Claude 68 is ISC-licensed; the full
upstream licence text is in `THIRD_PARTY_LICENSES`.

Upstream describes the engine as reusable boilerplate; the only thing this
project changes is how bytes reach the wire. Everything above the socket —
`HttpClient`, `HttpResponse`, `Uri`, `RootCert`, `gason`, `http_parser`,
`yuarel`, `uECC`/`uecc_alt`, `curve25519_sat`, `fast_alloc`, `mac_entropy`,
`tls_cache.h`, the prefs cache store, and the mbedTLS 3.2.1 archives — is
unmodified unless listed below.

## What was copied

| Here | Upstream |
|---|---|
| `net/HttpClient.{cpp,h}` | `src/core/net/` |
| `net/HttpResponse.{cpp,h}` | `src/core/net/` |
| `net/Uri.{cpp,h}` | `src/core/net/` |
| `net/RootCert.{cpp,h}` | `src/core/net/` |
| `net/gason.{cpp,hpp}` | `src/core/net/` |
| `net/http_parser.{c,h}` | `src/core/net/` |
| `net/yuarel.{c,h}` | `src/core/net/` |
| `net/uECC.{c,h}`, `net/uECC_vli.h`, `net/uecc_alt.c` | `src/core/net/` |
| `net/curve25519_sat.c`, `net/curve-specific.inc`, `net/platform-specific.inc`, `net/types.h` | `src/core/net/` |
| `net/vli_mult_comba_m68k.s`, `net/vli_square_comba_m68k.s`, `net/solinas_p256_m68k.s` | `src/core/net/` |
| `net/fast_alloc.c`, `net/mac_entropy.{c,h}` | `src/core/net/` |
| `net/transport_mactcp.c` | `src/core/net/net_sockets_mac.c` (renamed) |
| `helpers/prefs_io.cpp`, `helpers/growzone.{cpp,h}`, `helpers/tls_cache.h` | `src/core/helpers/` |
| `mbedtls/include/**`, `mbedtls/libmbed{crypto,tls,x509}.a` | `src/core/lib/mbedtls/` (mbedTLS 3.2.1) |
| `mactcp/*.h`, `mactcp/libMacTCPHelper.a` | `src/core/lib/mactcp/` |

Not copied, by upstream's own boundary: `src/api/`, `src/ui/`,
`src/core/helpers/helpers.cpp` (it includes Claude 68's `src/ui/menu.h`
and nothing in the engine calls it), `src/core/sse.cpp` (the app owns
stream parsing), `src/core/build_stamp.cpp`, and `src/config.h`.

`mactcp/` is present for two reasons: `transport_mactcp.c` links its
`libMacTCPHelper.a`, and `HttpClient.cpp` includes `<MacTCP.h>` and
`<mactcp/*.h>` unconditionally for `GiveTimePtr` and the TCPiopb-based
helper prototypes it calls by name. The library is linked only when
`-DNET68K_TRANSPORT=mactcp` is selected.

## Diffs applied to vendored files

### `net/transport_mactcp.c`
Renamed from `net_sockets_mac.c`. Contents unmodified.

### `net/HttpClient.cpp`
Two include lines, no code changes:

- `#include "Settings.h"` → `#include "tls_cache.h"`. Upstream's
  `Settings.h` is a two-line app-preferences header whose only relevant
  content is that it includes `tls_cache.h`; `HttpClient.cpp` uses
  `CachedDNS` and `PersistedTLSSession` and nothing else from it.
- Added `#include <Events.h>` for `SystemTask`.

### `helpers/prefs_io.cpp`
This is the engine's persistent cache store (DNS cache, ECDSA verify cache,
TLS session ticket, client random seed). Upstream keeps the application's
API key in the same file as the first record. The API key belongs to the
application, not the engine, so that record was removed:

- `#include "Settings.h"` and `#include "config.h"` →
  `#include "net68k_prefs.h"` and `#include "tls_cache.h"`.
  `net68k_prefs.h` is new and supplies only the two macros the file took
  from upstream's `config.h`: the HFS file name and creator code.
- Added `#include <Script.h>` for `smSystemScript` (upstream got it
  transitively through its app headers).
- `kSettingsOffset` removed; the following offsets shift down by
  `sizeof(AppSettings)` accordingly.
- `PrefsLayoutWord()` no longer folds `sizeof(AppSettings)` in. The layout
  word changes, so any existing cache file is discarded once — which is
  what it exists for.
- `ApiKeyLooksValid()`, `LoadSettings()` and `SaveSettings()` removed, and
  the api-key salvage in `EnsurePrefsHeader()` reduced to the truncate +
  fresh-header path.

The application implements its own `Prefs_*` in `src/prefs.c` over plain
`FSRead`/`FSWrite` with a `key=value` text file, exactly as the plan's
contingency allowed; it does not share this file.

### `net/types.h` → `net/uecc_types.h`
Renamed, and the two `#include "types.h"` in `net/uECC_vli.h` and
`net/platform-specific.inc` follow it. Contents unmodified.

Reason: it is micro-ecc's own header, unrelated to the Toolbox `<Types.h>`,
but on a case-insensitive filesystem (macOS) `#include <Types.h>` from any
translation unit that has `net/` on its include path resolves to it instead
of the system header, and nothing compiles. The guard macro `_UECC_TYPES_H_`
already says what the file is.

### New files
- `net/transport_ot.c` — Open Transport implementation of the same
  symbols `transport_mactcp.c` defines, selected with
  `-DNET68K_TRANSPORT=ot` (the default).
- `helpers/net68k_prefs.h` — see above.
- `CMakeLists.txt` — builds `libnet68k.a` and picks exactly one transport.

## Build flags

Copied from Claude 68's `CMakeLists.txt`, where they are the ones measured
to keep a handshake inside Cloudflare's ~15 s window on a 16 MHz 68030:

```
UECC_FLAGS = -m68020 -O3 -Dasm_mult=1 -Dasm_square=1 -Dasm_solinas_p256=1
             -DuECC_OPTIMIZATION_LEVEL=2 -DuECC_SQUARE_FUNC=1
             -DuECC_WORD_SIZE=4 -DuECC_SUPPORTS_secp256r1=1
             -DuECC_SUPPORTS_secp{160,192,224,256k1}r1=0
             -DuECC_VLI_NATIVE_LITTLE_ENDIAN=0      (uECC.c, uecc_alt.c)
*.s        = -m68020
curve25519_sat.c = -m68020 -O3 -Dsat_vli_asm=1
```

`CMAKE_C_STANDARD 99` is required: `mactcp/TCPRoutines.h` does
`typedef int bool` in C, which a C23 compiler rejects.

## Why Open Transport and not MacTCP

The engine upstream talks MacTCP, because that is what the machines it
targets run. Mac OS 8 and 9 do not ship MacTCP at all — Open Transport
replaced it — and Mac OS 9 is what the emulator harness runs. So the
default transport is `transport_ot.c`; `transport_mactcp.c` stays in the
tree, builds with `-DNET68K_TRANSPORT=mactcp`, and is the escape hatch for
a System 7.0.1 machine with MacTCP installed.

Open Transport needs Apple's Universal Interfaces for the 68k build; see
the top-level README. They are staged into the Retro68 toolchain and put
on this library's private include path only, so the application's own
translation units stay on Retro68's Multiversal headers.

## Trust store and known limitations

- The trust anchor is the Google Trust Services **WE1** intermediate,
  pinned in `net/RootCert.cpp` so only one P-256 signature is ever
  computed for the chain (the P-384 root check does not fit the timeout).
  `kGoogleWE1_DER` expires **2029-02-20**.
- Certificate **dates are not checked**: `MBEDTLS_HAVE_TIME_DATE` is off.
- There is **no revocation checking**.
- `libmbed*.a` were built by upstream for this toolchain. If they ever
  fail to link, rebuild them at the same version with upstream's
  `scripts/rebuild_mbedtls.sh`; the ALT hooks and the disabled modules are
  what make the handshake fit, so the version must not change.
