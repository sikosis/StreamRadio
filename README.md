<p align="center">
  <img src="graphics/Radio.png" alt="StreamRadio2" width="256">
</p>

# StreamRadio2 [![platform](https://img.shields.io/badge/platform-Haiku-blue)](https://www.haiku-os.org/) ![build](https://img.shields.io/badge/build-active-brightgreen) [![license](https://img.shields.io/badge/license-GPLv3-blue)](https://www.gnu.org/licenses/gpl-3.0.html) ![version](https://img.shields.io/badge/version-2.0-orange)

StreamRadio2 is a native Haiku application for finding and listening to internet
radio stations.

Search the [Community Radio Browser](https://www.radio-browser.info/) directory,
add stations to your collection, inspect and edit station details, and control
the volume of each stream. StreamRadio2 also supports direct Shoutcast URLs and
optional parallel playback of multiple stations. A sleep timer can stop or mute
playback, or quit StreamRadio2, at the top of the hour or after a preset delay.
The main window also offers mini-player, maximum-height, and fullscreen modes.

![StreamRadio2 station list and station information](docs/screenshots/StreamRadio.png)

---

This project was originally written by a dedicated team of talented coders who
laid a strong and thoughtful foundation. After the original project became
dormant, this independent fork began modernising that foundation for current
Haiku releases, improving stream reliability and continuing development without
being constrained to maintenance-only changes.

**The goal is simple:** to make it the best internet radio app on Haiku.

Credit to the original authors and past contributors, whose work this builds
directly upon.

> Sikosis · **Maintainer** · [GitHub](https://github.com/sikosis)

---

## Building on 32-bit Haiku

> [!IMPORTANT]
> When building on 32-bit Haiku, change the build environment to `gcc11` with
> `setarch x86` before invoking `make`.

---

## Build

From the repository root, run:

```sh
mkdir -p dist
cd source
make
make bindcatalogs
```

The application is written to `dist/StreamRadio2`.

For a 32-bit build, run the build commands through `setarch`:

```sh
mkdir -p dist
cd source
setarch x86 make
setarch x86 make bindcatalogs
```

---

## Package

On Haiku, build an installable package after compiling StreamRadio2:

```sh
cd source
make package
```

The package is written to:

```text
dist/streamradio2-2.0-<architecture>.hpkg
```

Install it with `pkgman`, substituting the generated filename:

```sh
pkgman install ./dist/streamradio2-2.0-x86_64.hpkg
```

StreamRadio2 has its own package and application identities and replaces the
legacy HaikuPorts `streamradio` package. This prevents a HaikuPorts update from
restoring the old application over this edition. Existing StreamRadio settings
and stations are preserved when upgrading.

> [!WARNING]
> The package target recreates its staging directory. Keep any files you need
> outside `dist/package`.

---

## Documentation

For installation and usage instructions, see the
[StreamRadio2 user guide](docs/userguide.md).

Bug reports and feature requests are welcome in the
[StreamRadio2 issue tracker](https://github.com/sikosis/StreamRadio/issues).

---

## License

StreamRadio2 is free software licensed under the
[GNU General Public License, version 3](https://www.gnu.org/licenses/gpl-3.0.html).
Third-party components and their licenses are listed in the application's
About window.
