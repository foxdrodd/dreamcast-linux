# Userland build configuration

This is the build and target configuration for the T2SDE distribution build toolkit to build the userland.

## config
Put the configuration file into this place:
`/usr/src/t2-src/config/yourtarget/config`

either:
musl- or uclibc-config to choose the desired libc.

## target
Put the target/dc folder under:
`/usr/src/t2-src/target/dc`

The default config uses "base" as the package preselection template, with
our defined packages.
