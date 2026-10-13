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

- Merge changes that are intended to affect all version branches into `main`.
- To apply these changes to other version branches:
  - Checkout and update the local `main` branch.
  - Run `.\ci\rebase.ps1`.
 
## Building All Versions to AWS

- Run `./ci/start_builds.ps1` with a parameter for your GitHub PAT.

## Creating Internal Builds

To create internal builds, simply follow the publishing steps below and then stop after finishing 'Create Internal Meta'.

## Publishing

Always follow these steps in order

1. Rebase
2. Build All
3. Run 'Create Internal Meta' github action
4. Run 'Publish'

After publishing, the public metadata that defines which revision is the latest needs to be updated. See updater service readme.
