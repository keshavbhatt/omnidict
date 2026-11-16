# Licensing

Omnidict is open source. Two licences apply, by path.

## The program: GPL-3.0-or-later

Everything in this repository except the licensing module (next section) is licensed
under the GNU General Public License, version 3 or, at your option, any later version.
The text is in `LICENSES/GPL-3.0-or-later.txt`. You may use, study, change
and share it under the GPL's terms: derived works stay under the GPL and come with their
source.

### Additional permission under GNU GPL version 3 section 7

If you modify this program, or any covered work, by combining it with the Ktechpit
Licensing Module (the files listed below), the licensors of this program grant you
additional permission to convey the resulting work, provided the Module is unmodified and
the resulting work is an official build published by Ktechpit. A work under the GPL that
is built or conveyed by anyone else must not contain the Module.

## The licensing module: Ktechpit Licensing Module License

The account, licence and evaluation-period code is not part of the GPL-licensed work. It is
source-available under the Ktechpit Licensing Module License
(`LICENSES/LicenseRef-Ktechpit-Licensing-Module.txt`): you may read it and build it
unmodified for your own use, and you may not modify it, bypass it, redistribute it or use
its endpoints in other software. These paths:

- `app/src/modules/AccountAndLicense/` (the module, as Ktechpit's other apps use it)
- `app/src/services/licensing/` (Omnidict's use of it)
- `app/src/core/licensing/` (reserved, empty)

The module is built only into the Microsoft Store package (`OMNIDICT_LICENSING`, on for
Windows, ADR-024). Every other build (the snap, the Flatpak, the AppImage, a distribution
package or your own) leaves it out and has no Omnidict Pro: everything is included there. The
one-dictionary rule of Omnidict Free is GPL code (`app/src/ui/free_limit.*`) that does nothing
without the module.

`REUSE.toml` records the same mapping in machine-readable form; the module's own files carry
an SPDX header.

## What this means for forks and contributors

- Contributions to the GPL part are welcome under the GPL.
- A fork that ships its own builds leaves the module out (the default everywhere but
  Windows) and must not talk to Ktechpit's licence servers.
- The Microsoft Store package published by Ktechpit combines both parts under the additional
  permission above; the Linux packages do not contain the module.

No earlier version of Omnidict existed under a different licence; this is the licensing model
from the project's start.
