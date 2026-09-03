# CI/workflows

Step bodies for `.github/workflows/`. Nothing in here is part of the release path.

The scripts one directory up (`CI/pipeline.ps1`, `CI/release.ps1`, `CI/make_installer.ps1` and
friends) are the build and release tooling, and several are run by hand. These are separate so
that the two sets do not get confused for each other.

Each script is one step of a workflow, pulled out of the YAML so it can be read, reviewed and
run outside of Actions. A workflow step should stay a single line calling one of these.

| Script | Step |
| --- | --- |
| `archive_name.sh` | Names the prebuilt OBS archive. **`ARCHIVE_VERSION` lives here** - bump it to invalidate every published archive. |
| `obs_archive.sh` | `restore` / `create` / `publish` the prebuilt OBS archive. |
| `clone_obs.ps1` | Clone OBS at the tag in `obs.ver`. |
| `build_obs.ps1` | Configure and build OBS with no plugin in the tree. |
| `graft_plugin.ps1` | Copy the plugin into `obs-studio/plugins` and register it with CMake. |
| `build_plugin.ps1` | Configure the grafted tree and build it, `-PluginOnly` against a prebuilt OBS. |
| `toolchain_manifest.ps1` | Record what the archive was built against, for drift diagnosis. |
| `install_rundir.ps1` | `cmake --install` into OBS's rundir so the suites can launch it. |
| `assert_no_obs_rebuild.sh` | Warn if a restored archive stopped avoiding an OBS rebuild. |
| `obs_log_tail.ps1` | Tail the OBS log after a failed run. |
| `check_format.sh` | clang-format report and fixup patch. |

## Running them locally

They take the same defaults CI uses - a plugin checkout in `obs-sl-browser/` and an OBS tree in
`obs-studio/`, side by side - and every path is a parameter, so from a directory laid out that
way they work unchanged:

```powershell
./obs-sl-browser/CI/workflows/clone_obs.ps1
./obs-sl-browser/CI/workflows/build_obs.ps1
```

For everyday plugin work you almost certainly want `CI/dev_build.ps1` instead, which keeps one
OBS checkout and rebuilds only the plugin.

## The prebuilt OBS archive

`obs.ver` pins the OBS tag, and OBS takes ~11 minutes to build. Rather than rebuild it in every
pull request, the e2e job restores a published archive of an already-built OBS tree and compiles
only the three plugin targets.

The archive deliberately contains **no plugin artifacts** - OBS is built with the plugin absent
from the tree entirely. That is what makes it safe: there is no stale plugin DLL to link,
because the plugin's objects do not exist until the run compiles them.

When nothing is published under the current name, the e2e job builds OBS itself and publishes
it, so the next run is fast. Publishing therefore only ever happens when the archive is
*missing* - a pull request cannot overwrite one that already exists. Use the workflow's
`refresh_archive` input to deliberately rebuild.

Two OBS quirks would otherwise recompile all of libobs on every run, and `build_obs.ps1`
pins both. See the comments in it before changing how OBS is configured.
