# Cricket 26 through CrossOver on Apple Silicon

This method ran the owned Microsoft Store **PC/GDK edition** of Cricket 26 through CrossOver, without booting Windows. The original installation passed real Microsoft/Xbox sign-in, loaded an England vs Australia match, and rendered an on-field delivery. The user confirmed that the game worked.

This repository contains source, patches and preparation scripts. It does not contain the game, Microsoft binaries, credentials, license tokens, saves or a preconfigured bottle. The procedure below reconstructs the working installation; a clean-Mac installation has **not** been tested. Online multiplayer, Xbox cloud saves, every game mode and long-session stability remain unverified.

## Working versions

| Component | Observed version |
| --- | --- |
| Host | Apple Silicon, macOS 27.0 build 26A428 |
| CrossOver | 26.3, build 26.3.0.39832 |
| Bottle | Windows 10 x64; `Cricket26-Runtime-Test` |
| Graphics | DXMT, with Rosetta translation |
| Game package | Microsoft Store PC/GDK version `3.69.0.0` |
| Game title screen | Version `1.0.3504`, online version `116` |
| Public Store product | `9N6JF50HZFW9` |
| Public Xbox TitleId | `7860b92b` hexadecimal |
| WineGDK | `b5d23b074cfd5e28e79acceaaefaf41a26ce6272` plus the included patch |
| Xodus | `0670e25aeb0e0e9f800f8f2f4968ae3b681842a7` plus the included patch |
| Native threading runtime | Microsoft Gaming Services `35.116.29001.0`, x64 |
| Rust | `1.98.0` |
| Build tools | Apple clang `21.0.0`, Homebrew LLVM `22.1.8`, Bison `3.8.2`, make, flex, protoc |
| macOS SDK | `MacOSX26.5.sdk` |

Use the source commits, not a current branch tip or historical PR label. The local WineGDK patch is complete; do not add the earlier native5 patch. CrossOver and the native runtime expose interfaces whose compatibility must be checked again after upgrades.

## 1. Choose paths and install prerequisites

Run the commands from a checkout of this repository. Set `GAME_ROOT` to a directory with sufficient space; the example is a choice, not a required username or location.

```sh
REPO_ROOT="$PWD"
GAME_ROOT="$HOME/Games/Cricket-26"
XODUS_SOURCE="$HOME/.local/share/xodus/source"
mkdir -p "$GAME_ROOT/runtime-research"
export XODUS_LOG=off
umask 077
```

Install the verified CrossOver version and enable Rosetta if needed. Install Xcode, its command-line tools, Homebrew LLVM and Bison, Rust 1.98.0 through rustup, and `protoc`. System `make` and `flex` were sufficient on the original Mac. The build uses two compilers: Apple clang for the **x86_64 Mach-O** bridge and LLVM clang for **Windows AMD64 PE** modules.

The templates retain `/Applications/Xcode.app`, `/Applications/CrossOver.app`, `/opt/homebrew`, and the SDK26.5 path. Inspect these prerequisites before building. If the SDK is unavailable, obtain a compatible SDK/toolchain through the normal Xcode installation process. The original attempt to use SDK27 with the earlier linker failed on the `arm64e.x1` architecture. A newer combination may work, but is not the recorded configuration.

The original package is approximately 98.34 GiB and extracted content approximately 96.85 GiB. Keeping both requires about **195 GiB**, before temporary files, source and build products. Check actual free space with `df -h "$GAME_ROOT"`. Old simulator/VM cleanup was incidental and should not be repeated automatically.

## 2. Pin the sources and apply the patches

Use clean destinations. These commands must not be run as a way to reset an existing modified checkout.

```sh
git clone https://github.com/xodus-gaming/xodus.git "$XODUS_SOURCE"
git -C "$XODUS_SOURCE" checkout 0670e25aeb0e0e9f800f8f2f4968ae3b681842a7
git -C "$XODUS_SOURCE" apply --check "$REPO_ROOT/source/xodus-real-store-license.patch"
git -C "$XODUS_SOURCE" apply "$REPO_ROOT/source/xodus-real-store-license.patch"

git clone https://github.com/Sightem/WineGDK.git "$GAME_ROOT/runtime-research/winegdk-source"
git -C "$GAME_ROOT/runtime-research/winegdk-source" checkout b5d23b074cfd5e28e79acceaaefaf41a26ce6272
git -C "$GAME_ROOT/runtime-research/winegdk-source" apply --check "$REPO_ROOT/source/winegdk-macos-local.patch"
git -C "$GAME_ROOT/runtime-research/winegdk-source" apply "$REPO_ROOT/source/winegdk-macos-local.patch"
```

If a checkout is already patched, inspect it rather than applying the patch twice. `git apply --reverse --check PATCH` is a read-only way to check whether a patch matches its current files.

Prepare the destination scripts and examples:

```sh
python3 "$REPO_ROOT/scripts/prepare.py" "$GAME_ROOT" --xodus-source "$XODUS_SOURCE"
```

Preparation renders `@GAME_ROOT@` and `@HOME@` templates, creates the build-script layout including `toolbin/lld-link`, and places the extraction example in `XODUS_SOURCE/crates/xodus-cli/examples/`. It refuses to overwrite differing existing files and accepts identical files on repeat runs. It does not supply the game, native Microsoft runtime or account credentials.

Preparation installs the included `source/Cargo.lock`, whose recorded SHA-256 is `d660974dcb7eaf2490d76f14a8ef83d1d73e52b0bcbdb1750d67f9882ac7d0d6`. The base checkout's lockfile can differ. If preparation reports that difference in a new reconstruction checkout, deliberately preserve the original under a nonexisting backup name, then rerun preparation. Do not silently replace a lockfile in a modified development checkout.

## 3. Build Xodus and complete normal login

```sh
cd "$XODUS_SOURCE"
"$HOME/.cargo/bin/cargo" +1.98.0 build --release --locked -p xodus-cli -p xodus-service
"$HOME/.cargo/bin/cargo" +1.98.0 build --release --locked -p xodus-cli --example cricket_inspect
"$HOME/.cargo/bin/cargo" +1.98.0 test --release --locked -p xodus licensing::store::tests
"$HOME/.cargo/bin/cargo" +1.98.0 test --release --locked -p xodus-service connection::store::tests
```

Use `--offline` only after dependencies have been cached. A different Cargo found on PATH was too old for the recorded build. Do not enable the `key-chain-file` feature: this method uses the normal macOS Keychain rather than a plaintext credential file. Diagnostic features such as `tokio_console` are unnecessary.

Log in through the freshly built CLI:

```sh
XODUS_LOG=off "$XODUS_SOURCE/target/release/xodus-cli" login
```

Use the owning Microsoft account and complete any normal browser/device-code and Keychain prompts. Enter passwords in the operating-system or provider dialog, never in these scripts. A newly compiled executable path can require a fresh Keychain approval. Neither the repository nor the compatibility bridge grants an entitlement.

Complete login before starting the service, with no concurrent account changes. The Store bridge captures a fixed account/device snapshot for each service session; changing the selected account or refreshing expired credentials requires a deliberate service restart. The PC Store account is independent of the Xbox profile handle.

## 4. Download and extract the owned PC package

Skip this section if the verified extracted game already exists.

The recorded package is:

```text
BigbenInteractiveSA.3997493E05F07_3.69.0.0_x64__tqjv3vrxr8ppw.msixvc
size:   105586794496 bytes
SHA256: d6d9b7569035c8b7045cc3af6647ed749b549bb7884ae5d15c0682cbb2cc6d72
```

The hash is a locally recorded checkpoint, not an independently published Microsoft checksum. Use the guarded resumable downloader:

```sh
cd "$REPO_ROOT"
python3 scripts/download.py "$GAME_ROOT"
```

The retained version-specific CDN URL may expire. If it fails, obtain current package metadata through authenticated tooling for the owned PC product. Do not replace a different package under the old filename/hash or assume that a new game build has the same runtime ABI. The downloader does not authenticate or decrypt the package.

Extract through Xodus, then extract the executable entries that the ordinary extractor intentionally leaves encrypted:

```sh
PACKAGE="$GAME_ROOT/BigbenInteractiveSA.3997493E05F07_3.69.0.0_x64__tqjv3vrxr8ppw.msixvc"
INSPECT_EXE="$XODUS_SOURCE/target/release/examples/cricket_inspect"
XODUS_LOG=off "$XODUS_SOURCE/target/release/xodus-cli" extract "$PACKAGE" "$GAME_ROOT/extracted" --market neutral
XODUS_LOG=off "$INSPECT_EXE" "$PACKAGE" "$GAME_ROOT/extracted"
```

Use a fresh destination. The example reuses the package parser, legitimate account content license and AES-XTS implementation. It writes each encrypted entry to a newly created `.decrypting` file, flushes it, then renames it over the encrypted target. A leftover `.decrypting` file is an error to investigate, not permission to delete files blindly.

Create the inventory directly from the package, then verify extraction:

```sh
python3 "$REPO_ROOT/scripts/inventory.py" "$GAME_ROOT" "$INSPECT_EXE" "$PACKAGE"
python3 "$GAME_ROOT/verify-extraction.py"
printf '%s  %s\n' \
  eb2a4db65977d15305136d8001466bcd2a9271b06216833b22c77833a07cb2b9 \
  "$GAME_ROOT/extracted/cricket26.exe" | shasum -a 256 -c -
```

The inventory script creates `progress.sqlite` from the inspection helper's package metadata. No excluded private database is required. The original extraction contained **151041 files**, totaling **103991445917 bytes**, with no missing or size-mismatched entries. Its AMD64 `cricket26.exe` SHA-256 was:

```text
eb2a4db65977d15305136d8001466bcd2a9271b06216833b22c77833a07cb2b9
```

This verifies package inventory/size completeness and the executable hash, not a cryptographic hash of every extracted file. Require these checks: the original extraction CLI could return zero despite an inner extraction failure. Preserve the game's original `MicrosoftGame.config`; the runtime reads its actual TitleId, StoreId and application settings.

## 5. Build the WineGDK modules

Preparation must create `runtime-research/winegdk-build/toolbin/lld-link`, with executable permission. This wrapper uses Rust 1.98.0's `rust-lld -flavor link` and matching toolchain libraries. Check the rendered paths before configuring.

```sh
sh "$GAME_ROOT/runtime-research/winegdk-build/configure-build.sh"
sh "$GAME_ROOT/runtime-research/winegdk-build/rebuild.sh"
```

The configure script chooses x86_64 macOS CC/CXX and SDK26.5. The build command is:

```sh
make -j6 dlls/xgameruntime/all dlls/windows.web/all
```

This builds the replacement runtime and Windows.Web parser, not a complete replacement CrossOver installation. X11, FreeType, GnuTLS, OpenGL, Vulkan, GStreamer and FFmpeg are disabled in this module build; CrossOver supplies the game's graphics runtime. The Linux/container build experiments are not part of this procedure.

The result must contain both:

- `dlls/xgameruntime/x86_64-windows/xgameruntime.dll`
- `dlls/xgameruntime/xgameruntime.so`

They share an IPC entry-point table and must be staged together after changes. The build also produces `dlls/windows.web/x86_64-windows/windows.web.dll`.

## 6. Obtain Microsoft's native threading DLL

Independently obtain the [official Microsoft GDK April 2026 Update 4 release](https://github.com/microsoft/GDK/releases/tag/April-2026-Update-4-v2604.4.7897). In its Gaming Services installer bundle, extract the **x64** `Microsoft.GamingServices` package version **35.116.29001.0**. AppX/AppXBundle archives can be inspected with `unzip -l`; select the x64 package rather than ARM64 or a resource-only package.

Retain its original `xgameruntime.dll`. The working file's SHA-256 is:

```text
aa611155057ebd01cf315ad702a4b5725aa9d5e6fad87732c956fe0a1e5fcfba
```

This DLL is not in the repository. It is used unmodified under the name `xgameruntime.dll.threading` for native XAsync/task queues. The complete native Gaming Services stack did not initialize under the tested CrossOver version; registering those services is not a prerequisite for the successful route.

## 7. Create and configure the isolated bottle

Create a Windows 10 x64 CrossOver bottle named `Cricket26-Runtime-Test`. Install the Visual C++ x64 runtime and select DXMT graphics. Keep other existing bottles intact.

```sh
BOTTLE="$HOME/Library/Application Support/CrossOver/Bottles/Cricket26-Runtime-Test"
CROSSOVER="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
RUNTIME="$GAME_ROOT/runtime-research/winegdk-runtime"
mkdir -p "$RUNTIME/x86_64-windows" "$RUNTIME/x86_64-unix"
```

Copy the verified Microsoft DLL to:

```text
<BOTTLE>/drive_c/windows/system32/xgameruntime.dll.threading
```

Create a symbolic link from `$RUNTIME/x86_64-unix/ntdll.so` to `$CROSSOVER/lib/wine/x86_64-unix/ntdll.so`. Use CrossOver's actual ntdll; do not overwrite it or substitute the newly built Wine ntdll.

With bottle applications closed, back up `cxbottle.conf`. Set its `[Wine]` `DllPath` to the following colon-separated directories, replacing placeholders with actual paths:

```text
<GAME_ROOT>/runtime-research/winegdk-runtime:<GAME_ROOT>/runtime-research/winegdk-runtime/x86_64-windows:<CROSSOVER>/lib/wine/x86_64-windows:<CROSSOVER>/lib/wine/i386-windows:<CROSSOVER>/lib/wine
```

CrossOver overwrote an ordinary `WINEDLLPATH` environment override during the investigation; the bottle configuration is required. The resulting layout is:

```text
winegdk-runtime/
  x86_64-windows/xgameruntime.dll
  x86_64-unix/xgameruntime.so
  x86_64-unix/ntdll.so -> CrossOver's existing ntdll.so
```

Stage the built modules:

```sh
python3 "$GAME_ROOT/runtime-research/stage-winegdk.py"
```

The staging script checks that the game is stopped, the official threading file exists and the bottle's runtime path is configured. It validates all build inputs before replacing files and records hashes in `progress.sqlite`. Individual replacements are atomic; the entire group is not one filesystem transaction. Keep the game stopped throughout.

The script also installs `windows.web.dll` in system32 and clears **only bytes 64–79**, its Wine builtin marker. This lets CrossOver use the rebuilt JSON parser instead of redirecting to its older bundled implementation. Do not clear this marker in `xgameruntime.dll` or modify Microsoft's native DLL.

Set these string values through the bottle's registry editor:

| Registry key under HKCU | Name | Value |
| --- | --- | --- |
| `Software\Wine\Explorer\Desktops` | `Cricket26Window` | `1280x720` |
| `Software\Wine\AppDefaults\cricket26.exe\Mac Driver` | `CaptureDisplaysForFullscreen` | `N` |
| `Software\Wine\AppDefaults\cricket26.exe\Mac Driver` | `WindowsFloatWhenInactive` | `none` |

These settings keep the game in a Wine desktop and prevent inactive floating/fullscreen display capture.

## 8. Start the real service and run the probes

Start the patched service after login is complete:

```sh
XODUS_LOG=warn "$XODUS_SOURCE/target/release/xodus-service"
```

Keep this terminal open. The service must expose `/tmp/xodus.sock`, owned by the current user with mode **0600**. Approve any legitimate Keychain prompt locally. If a service already exists, inspect its PID and socket rather than launching a second copy or deleting its socket. The account snapshot is fixed until a deliberate restart.

In another terminal, restore the `GAME_ROOT` variable and build the probes:

```sh
sh "$GAME_ROOT/runtime-research/winegdk-build/build-auth-probe.sh"
sh "$GAME_ROOT/runtime-research/winegdk-build/build-gamesave-local-probe.sh"
sh "$GAME_ROOT/runtime-research/winegdk-build/build-store-license-probe.sh"
```

With the game closed, run each through the configured bottle, with the extracted game as the working directory:

```sh
CROSSOVER="/Applications/CrossOver.app/Contents/SharedSupport/CrossOver"
(
for PROBE in runtime-auth-probe.exe runtime-gamesave-local-probe.exe runtime-store-license-probe.exe; do
  WINEGDK_LOCAL_GAMESAVE=1 "$CROSSOVER/bin/wine" \
    --bottle Cricket26-Runtime-Test --workdir "$GAME_ROOT/extracted" \
    --dll 'xgameruntime=b;windows.web=n' --debugmsg '-all' \
    "$GAME_ROOT/runtime-research/$PROBE" || exit $?
done
)
```

Inspect each probe's checks and exit status; do not treat a partial loop as success. The auth probe uses actual silent authentication. The local-save probe uses a disposable test SCID and verifies binary persistence, reopening, stale handles, invalid paths and preservation after a deliberately failed commit. The Store probe requests a **real Microsoft license token**, checks asynchronous completion and buffer ownership, and never prints the token. Avoid repeatedly requesting fresh tokens without a reason.

These checks establish the components, not gameplay. The final check is entering an actual match.

## 9. Launch the game

```sh
sh "$GAME_ROOT/runtime-research/launch-local-windowed.sh"
```

The rendered launcher sets `WINEGDK_LOCAL_GAMESAVE=1`, uses the `Cricket26Window` Wine desktop, sets the extracted directory as workdir, and selects `xgameruntime=b;windows.web=n`. It refuses launch if a Cricket 26 process is already running; simultaneous launcher invocations are not serialized. It does not start Xodus or provide credentials; keep the service running.

Do not minimize the game during startup verification: the minimized game stopped advancing in the original investigation. Progress beyond sign-in may include loading and additional save writes. Confirm the match itself rather than inferring gameplay from a title screen or a successful license probe.

## Local saves, recovery and limits

The local backend writes under Windows:

```text
%LOCALAPPDATA%\WineGDK\LocalGameSave\v1\<authenticated XUID in hex>\<SCID>
```

These directories are private account data. Do not commit them, rename identity/SCID directories or assume another account can use them. Back up the enclosing `LocalGameSave` directory only while the game is closed; compare file counts and hashes. A stopped-bottle backup additionally preserves runtime and registry configuration.

The backend uses checksummed atomic commits, a **256 MiB** local quota and a **16 MiB** per-update limit. It does not synchronize Xbox cloud saves. The game passed `syncOnDemand=false`; this is a synchronization-mode flag in Microsoft's API, not an assertion that Microsoft's provider is local-only. This implementation deliberately provides a separate opt-in local backend.

For a regression:

1. Close the game and preserve the working runtime, source revision/patch, bottle configuration and saves before changing anything.
2. Check the service, account expiry and the auth/save/Store probes separately.
3. After a runtime rebuild, stage the PE DLL and Mach-O bridge together and rerun the probes.
4. Confirm actual gameplay again after a CrossOver, game or native SDK update.

For an intentional account change, close the game, inspect `lsof -n /tmp/xodus.sock` and `ps -p PID -o pid=,comm=`, then stop only the identified service with `kill -INT PID`. Complete normal login and restart the service/game. Do not use broad `pkill wine`, reset credential records, or unconditionally delete sockets.

Presence HTTP400 occurred even during successful gameplay; it was not the proven sign-in blocker. GDI `PrintWindow`/`BitBlt` captures were black under DXMT, so visual verification used normal macOS screen-capture permission. Neither issue justifies changing authentication or entitlement results.

No downloaded community save was needed or installed. No Xbox console image, UTM VM or full Windows Gaming Services installation is part of the working method. The chronology records the investigations that led here without requiring private logs or excluded binaries from the original machine.
