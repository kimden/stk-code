[![Linux build status](https://github.com/kimden/stk-code/actions/workflows/linux.yml/badge.svg)](https://github.com/kimden/stk-code/actions/workflows/linux.yml)
[![Apple build status](https://github.com/kimden/stk-code/actions/workflows/apple.yml/badge.svg)](https://github.com/kimden/stk-code/actions/workflows/apple.yml)
[![Windows build status](https://github.com/kimden/stk-code/actions/workflows/windows.yml/badge.svg)](https://github.com/kimden/stk-code/actions/workflows/windows.yml)
[![Switch build status](https://github.com/kimden/stk-code/actions/workflows/switch.yml/badge.svg)](https://github.com/kimden/stk-code/actions/workflows/switch.yml)

This repository contains a **modified** version of SuperTuxKart (STK), mainly intended for server-side usage, but you can also use it as a client.

Most important changes are listed [here](/FORK_CHANGES.md). Standard version of STK can be found [here](https://github.com/supertuxkart/stk-code/), and the changes between it and latest commits of this repo can be found [here](https://github.com/supertuxkart/stk-code/compare/master...kimden:stk-code:master).

## Important branches

* [master](https://github.com/kimden/stk-code/): contains the latest version. For a few years, it contained old commits, while new commits were on `command-manager-prototype` branch. Use this branch if you want to deploy your own server.

* [command-manager-2x](https://github.com/kimden/stk-code/tree/command-manager-2x): the branch combining `command-manager-prototype` and [BalanceSTK2](https://github.com/Alayan-stk-2/stk-code/tree/BalanceSTK2), which is currently the source code of 2.X development version. This branch might not have the latest commits, its purpose is to store game results while testing 2.X version. It will be changed to have commits from `master` instead of `command-manager-prototype`, which is now deprecated.

* [local-client](https://github.com/kimden/stk-code/tree/local-client): the version of client with some optimizations and edits, used by kimden. For now, this is the only branch of this repo that is really intended for client-side usage.

There are also several less important branches.

[Tyre Mod Edition](https://github.com/Nomagno/stk-code/tree/tyre2X) by Nomagno is also using branches of this repository for both server and client updates.

---

The software is released under the GNU General Public License (GPL) which can be found in the file [`COPYING`](/COPYING) in the same directory as this file.

Building instructions can be found in [`INSTALL.md`](/INSTALL.md).

Info on experimental anti-troll system can be found in [`ANTI_TROLL.md`](/ANTI_TROLL.md).

---

## About SuperTuxKart

[![#supertuxkart on the libera IRC network](https://img.shields.io/badge/libera-%23supertuxkart-brightgreen.svg)](https://web.libera.chat/?channels=#supertuxkart)

SuperTuxKart is a free kart racing game. It focuses on fun and not on realistic kart physics. Instructions can be found on the in-game help page.

The SuperTuxKart homepage can be found at <https://supertuxkart.net/>. There is also our [FAQ](https://supertuxkart.net/FAQ) and information on how get in touch with the [community](https://supertuxkart.net/Community).

Latest release binaries can be found [here](https://github.com/supertuxkart/stk-code/releases/latest), and preview release [here](https://github.com/supertuxkart/stk-code/releases/preview).
