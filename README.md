## Build Instructions

1. Build OBS (clone recursive).
2. Copy this project into the OBS `plugins` folder.
3. Update OBS plugin folder's `CMakeLists.txt`.
4. Run CMake configure/generate again.
5. Build the plugin.

## Commits and PRs

- Merge changes that are intended to affect all version branches into `main`.
- To apply these changes to other version branches:
  - Checkout and update the local `main` branch.
  - Run `.\ci\rebase.ps1`.
