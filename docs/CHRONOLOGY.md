# How the working method was found

The goal was to run an owned Microsoft Store copy of Cricket 26 locally on an Apple Silicon Mac, without booting Windows, and enter a playable match. The investigation completed on 12 September 2026: the game loaded an England vs Australia match, rendered an on-field delivery, and the user confirmed that it worked.

This chronology distinguishes failed experiments from the final method. It does not require the original machine's private logs, screenshots, account metadata or databases. Rebuild and launch instructions are in [METHOD.md](METHOD.md). The repository contains source and scripts, not the game, Microsoft binaries or credentials.

## Product selection and the abandoned Xbox OS route

The initial discussion explored obtaining an Xbox offline restore image and trying it through UTM or Parallels. That work was later canceled. It did not establish a bootable Xbox OS VM and contributed no component to the successful method.

The exact owned Cricket 26 product was checked for PC/Xbox Play Anywhere coverage. The eventual payload was the **Microsoft Store PC/GDK package**, version `3.69.0.0`, not an Xbox console executable. This product distinction mattered before investing in download or compatibility work.

## Download, storage and extraction

The version-specific Microsoft CDN package was downloaded with resume support. Completion was checked against its recorded byte size and a locally computed SHA-256. Enough space was needed for both the original package and extracted content: approximately 195 GiB before tools and temporary files. Authorized simulator cleanup made room on the original Mac; deleting simulators is not a replay prerequisite.

Xodus obtained a legitimate account content license and extracted the package. Its normal extraction path intentionally retained some executable entries encrypted. A small `cricket_inspect.rs` example reused Xodus's package parser, license request and AES-XTS implementation to extract those entries into fresh temporary files, flush them and rename them over the encrypted targets.

Verification checked 151041 files totaling 103991445917 bytes against the package inventory, with no missing or size-mismatched entries. The executable had valid MZ/PE headers and AMD64 machine type. This established completeness and the recorded executable hash; it did not cryptographically compare every extracted file. The extra checks mattered because the original extraction CLI could exit zero after inner failures.

The source-only replay now regenerates the inventory from the package into a new SQLite database. It does not depend on a private database from the original installation.

## CrossOver and the first native runtime attempt

CrossOver 26.3, a Windows 10 x64 bottle, Visual C++ x64 runtime, Rosetta and DXMT allowed the PC executable to load. Reaching the title screen established basic executable/graphics compatibility, but sign-in still failed or waited.

Runtime experiments used a separate `Cricket26-Runtime-Test` bottle, preserving the original bottle and game assets. Microsoft's official GDK April 2026 Update 4 supplied Gaming Services `35.116.29001.0` x64 and its native `xgameruntime.dll`.

The native DLL loaded, but a runtime initialization probe returned `0x80040154` (class not registered). Registering the native GamingServices and GamingServicesNet executables as demand-start services did not solve their dependencies. Missing Windows components, unsupported software-device/WNF calls, and network-list marshaling failures remained. Re-registering the Wine network-list implementation did not resolve the latter.

The complete native Gaming Services route was abandoned. Its unmodified threading/task-queue implementation was retained as `xgameruntime.dll.threading`. Running the full native service stack is not required by the final method.

## Build a small WineGDK replacement

WineGDK commit `b5d23b074cfd5e28e79acceaaefaf41a26ce6272` became the source base. Linux/container build attempts were superseded by a local macOS build: a Windows AMD64 runtime module plus an x86_64 Mach-O socket bridge.

The working build used SDK26.5 and a Rust 1.98 `rust-lld` wrapper for PE linking. SDK27 with the earlier linker failed on an unknown architecture. Only the required xgameruntime and windows.web modules were built; CrossOver continued supplying the application's graphics and general Windows compatibility runtime.

CrossOver replaced a simple environment DLL-search-path override, so the custom search path had to be written into the isolated bottle's configuration. The custom Unix directory linked to CrossOver's existing ntdll. The rebuilt Windows.Web DLL's Wine builtin marker was cleared so its JSON parser would load instead of CrossOver's older bundled version. The custom xgameruntime DLL retained its marker.

## Real Xbox user authentication

The compatibility runtime needed fixes for hexadecimal TitleId parsing, worker COM initialization, async completion and result ownership, IPC error handling and an authentication buffer overrun. The title-management endpoint was corrected, NULL cleanup was fixed and credential-bearing diagnostic output was removed.

The authentication probe then completed the actual Microsoft account, Xbox user-token, XSTS and profile flow. It returned a real user handle with a nonzero user ID and a profile. Unsupported age and privilege operations remained failures; they did not invent permission grants.

The game consumed the successful auth callback and immediately requested an unimplemented GameSave interface. The same sign-in spinner therefore concealed a different pending operation. A screenshot alone could not identify its cause.

## Local save compatibility

A failure-only GameSave adapter first demonstrated that a real asynchronous failure callback reached the game's error handling. It returned `E_NOTIMPL` with no provider. That diagnostic was not a save implementation.

Forwarding a custom WineGDK user handle into Microsoft's native GameSave implementation was unsafe because the native component expected its own internal user object. The game passed `syncOnDemand=false`; this is Microsoft's upfront synchronization mode, not an instruction to use a local-only provider.

A separate, explicit local backend was implemented behind `WINEGDK_LOCAL_GAMESAVE=1`. It binds storage to the authenticated user and SCID inside the isolated bottle. It implements real container enumeration, binary reads/writes, deletions and checksummed atomic updates. Bounds, path validation and handle tracking protect the local store. It does not claim Xbox cloud synchronization.

Tests verified binary round trips, invalid paths and handles, persistence after provider reopen, an independent process reading committed bytes, and preservation of earlier data after a deliberately failed locked update. Actual game save data was subsequently written and read.

A community team/profile pack was downloaded and inspected after the user suggested it, but was never installed. Its compatibility was not established and it was not needed for the successful method.

## Authenticated local-user lookup and window behavior

The next repeated missing operation was `XUserFindUserByLocalId`. A session-local registry provided lookup only for authenticated retained users, assigned nonreused local IDs, and safely removed a user after its final reference closed.

The probe established identity equality, survival after the original handle closed, rejection of unknown/stale IDs, and lookup failure after final close. The actual game then completed these lookups.

The game was placed in a 1280×720 Wine desktop with inactive floating and fullscreen display capture disabled. Minimizing it stopped startup progression; restoring it without activation allowed progress. GDI window captures were black under DXMT, so later visual verification used normal macOS screen-capture permission.

## Identify the last indefinite Store task

Static game analysis and bounded call-site logging found `XStoreCreateContext` followed by `XStoreQueryLicenseTokenAsync`. This particular task ignored immediate error returns and waited for a completion callback. The absent Store implementation therefore left it pending indefinitely.

A native-only Store probe could obtain the factory but could not create a usable context; it returned `0x89240100`. A successful social request and a presence HTTP400 occurred alongside the wait. Presence still returned HTTP400 after gameplay succeeded, so it was not the demonstrated blocker.

## Connect real Store licensing to native async completion

Xodus already contained a real Microsoft license-token helper. The final bridge connected that helper to WineGDK's Store API using the actual configured parent StoreId, the caller's ordered product IDs and its exact developer challenge.

The service snapshots the selected Store account and device credentials for a session. An opaque binding ties each Store context to that snapshot. This is independent of the Xbox profile handle, matching the PC Store account distinction. Login must finish before service startup, with no simultaneous account switching; the legacy account records are not updated atomically as one unit.

The existing shared IPC response mechanism was unsuitable for concurrent/late Store replies. The new path uses a dedicated connection per request, verifies socket ownership and same-user peer credentials, checks message type and size, enforces deadlines and avoids payload logs. XML serialization preserves the exact challenge, including whitespace and metacharacters.

The server performs genuine Microsoft account/device token exchanges and calls the licenseToken endpoint. Only Microsoft's returned token is forwarded, unchanged; server and transport errors remain failures. Native XAsync queues schedule work and deliver the callback, and the operation retains its context and copied inputs until cleanup. Unsupported Store operations do not synthesize purchases or ownership.

Two independent source reviews covered each final component. Focused tests covered request bounds, wire preservation and protocol roots, alongside the runtime ownership analysis. Those results apply to the recorded patch; later edits require their own review and verification.

## Keychain approval and end-to-end success

The first direct endpoint probe stopped inside a synchronous macOS Keychain call while waiting for local password approval. It was not a network stall. After the dialog was resolved, a fresh probe returned HTTP200 and a real Microsoft-issued token without printing or saving its contents.

The patched service was started and the PE/Unix runtime modules were staged together. The real service IPC request succeeded, followed by a native Wine probe verifying a pending result, no inline callback, exactly one completion callback, retained state after the public context closed and retrieval of the actual token.

The game itself then logged Store completion and result success. It passed Signing In, wrote additional local save data, loaded the stadium and entered an England vs Australia match. Visual evidence showed an on-field delivery with score and bowling feedback. The user confirmed the game was working.

That combination established the result: real authentication, real licensing, local persistence and actual match rendering. A title screen, green build or headless token probe alone would not have satisfied the goal.

## Preserve a repeatable source-only method

The original Mac's support files were preserved separately for local recovery. This repository contains the method, pinned patches, source examples and scripts that render the required paths and reconstruct package inventory. It intentionally excludes the game, native Microsoft DLLs, account data, private logs, screenshots and prebuilt executables.

Ordinary use on a reconstructed installation requires its extracted game, configured bottle, built runtime and running authenticated service. Rebuilding requires the pinned sources and compatible toolchains. A different Mac, CrossOver version, game package or native runtime needs fresh integration and gameplay checks; the documentation does not claim a tested clean-Mac installer.
