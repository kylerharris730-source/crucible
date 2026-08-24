# Crucible launcher and releases

The public entry point is `crucible-launcher.exe`. A player downloads that file
once from a GitHub Release and can keep using it for later releases.

On startup the launcher downloads `crucible-manifest.txt` from the repository's
latest full release. It installs the game under `%LOCALAPPDATA%\Crucible`, checks
both executable downloads with SHA-256, and replaces the installed game only
after the full new file has been verified. If GitHub is unavailable, an already
installed game remains available through **Play** as an offline fallback.
On first use it also copies any adjacent `crucible*.sav` files and
`crucible.cfg` into the install folder without overwriting data already there,
so moving from the direct executable does not strand existing worlds.

The launcher also compares its own hash with the release copy. A new launcher is
downloaded as `crucible-launcher.next.exe`; on the next open that verified copy
runs a replacement mode, waits for the old process to exit, installs itself, and
reopens normally. This two-process handoff is required because Windows will not
overwrite a running executable.

## Publishing

Releases are intentional, not every commit. Push a version tag:

```powershell
git tag v0.1.0
git push origin v0.1.0
```

The `Publish Windows release` workflow builds both executables and publishes
three fixed asset names:

- `crucible-launcher.exe`
- `crucible.exe`
- `crucible-manifest.txt`

The fixed names make GitHub's `/releases/latest/download/...` URL stable. Do not
rename those assets without updating `launcher/main.cpp` and the workflow in the
same release.

GitHub HTTPS plus the manifest hashes protects against interrupted or corrupted
downloads. It does not replace Authenticode signing: before presenting the
launcher as a broadly trusted public download, sign both executables and make
the workflow verify the signatures before publishing.

## Local build

Run `build.bat`, then `build_launcher.bat`. The results are:

- `build\crucible.exe`
- `build\crucible-launcher.exe`

Before the repository has its first GitHub Release, GitHub correctly returns
HTTP 404 for the `latest/download` manifest URL. For local iteration, the
launcher detects an adjacent `crucible.exe`, imports it into the normal install
folder, and enables **Play** with a development-build status message. This keeps
the release path honest while still allowing the launcher to be tested before
the first `v*` tag is published.
