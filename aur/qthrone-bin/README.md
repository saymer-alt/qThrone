# qThrone for Arch Linux

This binary package installs the existing qThrone 1.1.2 Linux release with
its bundled Qt libraries. No separate application release is required.
Supported release architectures: x86_64 and aarch64.

## Install from this repository

```sh
sudo pacman -S --needed base-devel git
git clone --branch packaging/archlinux https://github.com/ttrroyy/qThrone.git
cd qThrone/aur/qthrone-bin
makepkg -si
```

Start **qThrone** from the application menu or run `qthrone`.
Settings are stored in the user's configuration directory rather than
the root-owned installation at `/opt/qThrone`.
For TUN mode, a working polkit authentication agent is required to approve
the application's existing privilege setup.

Remove the package with `sudo pacman -R qthrone-bin`. User settings are preserved.
Use pacman/AUR package updates instead of the portable in-app updater.

## AUR submission

The package directory contains `PKGBUILD`, `.SRCINFO`, the launcher and desktop
entry. Submit these files to `ssh://aur@aur.archlinux.org/qthrone-bin.git`
using an AUR account with a registered SSH key. AUR hosts packaging recipes;
the application archives remain on GitHub Releases.

For a new upstream version, update `pkgver`, both architecture-specific
archive checksums and `.SRCINFO` (`makepkg --printsrcinfo > .SRCINFO`).
Changes to local source files also require updating their SHA-256 checksums.
