# Verification record

## Original installation

The original installation reached a real England vs Australia match on 12 September 2026. The user confirmed gameplay. Earlier checks separately exercised real account authentication, local save persistence, Microsoft Store license acquisition, and the native Wine async callback.

The final service returned a real 1953-byte license token with HTTP 200. Headless checks printed status and token length, not the token. These observations apply to the original installation and pinned artifacts; they do not guarantee a future account, service, SDK, or game build will behave identically.

## Source repository

Both included patches were applied to temporary clean archives of their pinned upstream commits and passed a reverse-apply check. Python syntax and rendered shell syntax passed. Offline checks exercise preparation twice, paths containing spaces, preserving a conflicting file, refusing a missing extraction inventory, avoiding a download for a completed file, preserving a bad completed file, and promoting a fully downloaded partial file after checksum validation.

Run the offline checks from the repository:

```sh
python3 scripts/check.py
```

Packaging changes replace machine-specific paths with renderable placeholders, add download/inventory/preparation helpers, suppress verbose launch tracing, and strengthen extraction verification with inventory, AMD64 and executable-hash checks. Staging now validates the effective DLL search order and uses exclusive temporary files. The runtime and service patches are preserved unchanged.

Additional offline staging checks use a disposable bottle and placeholder binary bytes. They reject an ineffective DLL configuration before writing, preserve an existing `.stage-new` symlink and its target, and verify the Windows.Web marker transformation. Preparation also tolerates incidental `.DS_Store` and `__pycache__` entries. These fixtures test filesystem behavior; they are not runtime execution tests.

No game files or compiled binaries are committed. No account login, new package download, game launch, complete runtime rebuild, or fresh-Mac installation was performed to publish this repository. The local binary recovery archive remains separate and private.

The revised extraction verifier also passed with Python optimization enabled against the existing game, using a temporary copy of the package inventory. It checked all 151041 file sizes, the AMD64 header and the exact recorded executable SHA-256. This read-only check did not alter the game or the original database.

Before publication, the staged text was scanned for the original local username/home path, personal name/email, order identifier, credential literals, JWTs and private keys. Two independent reviews covered publication privacy and reconstruction correctness. Git commits use the repository owner's public GitHub handle and GitHub noreply email, as requested, rather than a local system username or personal email. Machine-specific paths are rendered locally and are excluded from the source repository.

## Required validation after reconstruction

1. Verify the downloaded package against the recorded local SHA-256.
2. Generate the package inventory and verify extracted file sizes and executable hash.
3. Build both runtime components and stage them together with the game stopped.
4. Complete normal owning-account login, resolve any Keychain prompt, and start the patched service.
5. Run auth, local-save, and Store probes. A Store-only probe does not validate the other two paths.
6. Enter an actual match, play a delivery, then confirm save/reload behavior. A title screen or successful callback alone does not prove gameplay.

Cloud save synchronization and online multiplayer are outside the verified result.
