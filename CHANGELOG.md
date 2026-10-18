# Changelog

## [0.4.1](https://github.com/wgd-modular/carciofo-firmware/compare/v0.4.0...v0.4.1) (2026-10-18)


### Bug Fixes

* Read the CV inputs as 0 V to +5 V ([#17](https://github.com/wgd-modular/carciofo-firmware/issues/17)) ([f6776f6](https://github.com/wgd-modular/carciofo-firmware/commit/f6776f6fc996b9171737d095510f17aba6c47709))

## [0.4.0](https://github.com/wgd-modular/carciofo-firmware/compare/v0.3.0...v0.4.0) (2026-10-17)


### Features

* Add a PDF manual built from the firmware READMEs ([#12](https://github.com/wgd-modular/carciofo-firmware/issues/12)) ([a8616c3](https://github.com/wgd-modular/carciofo-firmware/commit/a8616c3da05b9fce5c2ee4a8b26d9fc53dd919d1))
* Add Eco, a clockable stereo delay ([#10](https://github.com/wgd-modular/carciofo-firmware/issues/10)) ([4e677cb](https://github.com/wgd-modular/carciofo-firmware/commit/4e677cbbb4bc7c56400f956791bbb0e455df8ee7))
* Add Nembo, Clouds for the Carciofo ([#9](https://github.com/wgd-modular/carciofo-firmware/issues/9)) ([6c657f7](https://github.com/wgd-modular/carciofo-firmware/commit/6c657f74c37dec56176e8d19eaa9879c22835e39))

## [0.3.0](https://github.com/wgd-modular/carciofo-firmware/compare/v0.2.0...v0.3.0) (2026-09-21)


### Features

* Add Battito In Acciaio, a percussive synth voice ([#7](https://github.com/wgd-modular/carciofo-firmware/issues/7)) ([d39b062](https://github.com/wgd-modular/carciofo-firmware/commit/d39b062c7ef5afa2e000844aee9d611a8a714b4d))


### Bug Fixes

* Catch short triggers in Doppio ([#8](https://github.com/wgd-modular/carciofo-firmware/issues/8)) ([8ac9a31](https://github.com/wgd-modular/carciofo-firmware/commit/8ac9a314358aa04f4a15d0ebf6937e920e3cf7a5))
* publish firmware binaries to gh-pages to fix web flasher CORS ([8de6d00](https://github.com/wgd-modular/carciofo-firmware/commit/8de6d005321217426576a6d0b6dfc110ccefa634))

## [0.2.0](https://github.com/wgd-modular/carciofo-firmware/compare/v0.1.0...v0.2.0) (2026-09-04)


### Features

* Add Doppio, a dual drum voice firmware ([#3](https://github.com/wgd-modular/carciofo-firmware/issues/3)) ([cbe9b85](https://github.com/wgd-modular/carciofo-firmware/commit/cbe9b856f290e4ada53e1a28dc1f0c991e1d0c57))

## 0.1.0 (2026-08-24)


### Features

* Add hardware layer for pots, CV, buttons and the RGB LED ([9594489](https://github.com/wgd-modular/carciofo-firmware/commit/95944898c55b7f355cece4942904763c309c1c45))
* Add hardware test firmware ([a192048](https://github.com/wgd-modular/carciofo-firmware/commit/a1920483cabf9b5dcb50f9d1a23b28f979c55ba3))
* Add Quattro, a four effect firmware ([717e83a](https://github.com/wgd-modular/carciofo-firmware/commit/717e83a8804f39cd3f6111bfaaf9561469bb779f))
* Add Quattro, a four effect firmware ([6414a96](https://github.com/wgd-modular/carciofo-firmware/commit/6414a967920d8fcc15915a68c3404b2bf9da3987))
* Add reverb with freeze and octave up shimmer ([d3c8392](https://github.com/wgd-modular/carciofo-firmware/commit/d3c8392f8dc53baf82f41397a95e187810fb38fd))


### Bug Fixes

* Cap the shimmer level so a frozen tank cannot clip the output ([e149758](https://github.com/wgd-modular/carciofo-firmware/commit/e14975843211fdf70814625774585917ca1bbdde))
* Stop the shimmer feedback from pushing the reverb tank into DC ([1ff542c](https://github.com/wgd-modular/carciofo-firmware/commit/1ff542c5a1cd2f24058a75e5c1d9193b9fd52f0e))
