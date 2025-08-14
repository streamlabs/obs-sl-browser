## Local Build Instructions

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
 
## Building All Versions to AWS

- Run `./ci/start_builds.ps1` with a parameter for your GitHub PAT.

## Publishing

Always follow these steps in order

1. Rebase
2. Build All
3. Run 'Create Internal Meta' github action
4. Run 'Publish'

After publishing, the public metadata that defines which revision is the latest needs to be updated. See updater service readme.
