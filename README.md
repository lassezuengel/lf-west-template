This repository contains various tests regarding the usage of 6LoWPAN within the `Lingua Franca` system, using the `reactor-c` framework. It is structured as follows:

- `py` contains python scripts that ease the use of this repository (e.g., automatic compilation and flashing of the tests) as well as some example scripts that we can use to evaluate network connectivity and performance (e.g., udp-based clients and servers).
- `apps` contains various applications, not necessarily related to LF. As of now, the LF 6LoWPAN examples and tests live in `apps/NrfBlinky/src/`, but we may want to move them to appropriate subfolders in the future.

# Lingua Franca West Template
Refer to the official [docs](https://www.lf-lang.org/docs/handbook/zephyr?target=c) for information on how to use these west-centric LF repositories.