## Dev Notes

Defining SL_PLUGIN_DEFAULT_URL as an env variable will override the default URL loaded by the plugin's browser.

## Local Build Instructions

1. Build OBS (clone recursive).
2. Copy this project into the OBS `plugins` folder.
3. Update OBS plugin folder's `CMakeLists.txt`.
4. Run CMake configure/generate again.
5. Build the plugin.

## Windows CI Packages

The CI build stages four runtime files from `build_x64/plugins/obs-sl-browser/RelWithDebInfo`:
`sl-browser-plugin.dll`, `sl-browser.exe`, `sl-browser-page.exe`, and
`streamlabs-app-icon.png`. The `.lib`, `.exp`, and `.pdb` files in that build directory are not
installer payloads. CMake also stages files in OBS's `rundir`; that tree has a different layout
from the compiler output directory.

For an OBS 33 installer, select the OBS installation root (the directory containing
`bin/64bit/obs64.exe`). The installer places the DLL in `obs-plugins/64bit`, which OBS 33 still
scans for third-party plugins, and the two helpers and icon in `core/obs-browser`, beside CEF.
Older OBS installers continue to install the four files into the selected plugins directory.
The signed ZIP contains the four flat runtime files for the existing update pipeline; it is
not an OBS installation tree.

## Commits and PRs

Merge changes meant for every OBS version into `main`. Each OBS version has its own branch (e.g. `32.1.1`), listed in [`obsversions_internal.json`](https://slobs-cdn.streamlabs.com/obsplugin/obsversions_internal.json).

## Releasing

A revision number covers all version branches together. State lives in [`meta_publish.json`](https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json).

1. **Rebase**: on an up-to-date `main`, run `.\ci\rebase.ps1`. It merges `main` into every version branch and pushes.
2. **Build all**: run `.\ci\start_builds.ps1 <GitHub PAT>`. It bumps `next_rev`, then runs `main.yml` on every version branch. Every build gets that revision.
3. **Create Internal Meta** (GitHub Action, run on `main`): writes the built revisions to `meta/internal_meta.json`. For an internal build, stop here and test.
4. **Publish** (GitHub Action, run on `main`): uploads per-branch metadata `meta/rev<N>_<branch>.json` and packages. It sets `new_release_rev = N` and `next_rev = N + 1`. It fails unless every version branch HEAD has a signed build, so don't push to version branches between steps 2 and 4. Set `chance_get_new` to 0 first; otherwise that share of users gets revision N immediately.
5. **Roll Out** (GitHub Action): This action sets the rollout fields of `meta_publish.json`. A blank revision keeps its current value, and a revision is refused unless it's published for every version branch.

Trigger only one release workflow (Publish, Create Internal Meta, increment, Roll Out) at a time: GitHub keeps one pending run per concurrency group, so a third queued run is silently dropped.
