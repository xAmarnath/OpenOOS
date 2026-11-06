# Source, runtime, and images

## Current local sizes

Measured while preparing this source snapshot on 6 November 2026. Sizes are
logical file sizes, not compressed transfer sizes or sparse disk allocation.

| Collection | Files | Bytes | Decimal GB |
| --- | ---: | ---: | ---: |
| Development workspace | 29,598 | 2,505,286,641 | 2.505 |
| Existing private bundle | 64 | 3,297,628,149 | 3.298 |
| Published controller runtime references | 52 | 58,426,292 | 0.058 |

The first two rows are separate collections with duplicated content; they should
not be added to estimate the download size. The workspace is not the complete
guest installation on the phone. The 32 GiB guest data capacity is not a GitHub
repository or download size.

The bundle's `guest-assets.tar.gz` is 3,251,226,393 bytes. It is private and stale:
of the 52 runtime references, 9 differ from that bundle and 5 are absent.
The hardware bridge reference includes a successful native rebuild that has not
been deployed to the guest. These sizes describe local inputs, not a tested release.
See [workspace groups](workspace-sizes.json) and [runtime files](runtime-inventory.json).

## Public repository

Publish source, authored configuration, dependency information, and documentation.
Keep images, userdata, credentials, signing keys, raw device logs, downloaded
toolchains, and proprietary APKs/libraries outside the repository. The Git ignore
file uses an explicit allowlist; intentionally adding a new source file requires
updating it and the source manifest.

The project license remains to be selected. External dependencies retain their
own licenses. Their availability on a device does not by itself establish rights
to redistribute them, and this source snapshot does not include them.

## External image hosting

The image will be hosted separately; **no public image URL is available yet**.
A future image release should identify the matching OpenOOS commit, firmware,
guest build, compressed/unpacked sizes, and SHA-256 checksum, and use a clean
data image without personal settings. The existing private backup must not be
uploaded as that release.

GitHub [blocks files larger than 100 MiB in normal Git](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-large-files-on-github).
GitHub [release assets must each be under 2 GiB](https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases).
External image hosting keeps those binaries separate from the source history.
