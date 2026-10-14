# Reproducible build-and-test environment for this Pintos kernel.
#
#   docker build -t pintos .
#   docker run --rm -v "$PWD/analysis/test-results:/results" pintos            # every project present
#   docker run --rm -v "$PWD/analysis/test-results:/results" pintos userprog   # one project
#
# The image holds only the toolchain and a copy of the repository; it never
# changes kernel source. Results land in /results/<project>/.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential gcc-multilib make perl \
      qemu-system-x86 \
      ca-certificates \
 && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04's GCC differs from Pintos-era compilers in two defaults that
# break an unmodified Pintos tree: it builds PIE (the kernel triple-faults at
# boot) and uses -fno-common (stock filesys.h then fails to link). A small gcc
# wrapper restores the old defaults for 32-bit Pintos builds only, so no
# Make.config or kernel file has to change. See scripts/gcc-no-pie.sh.
COPY scripts/gcc-no-pie.sh /usr/local/bin/gcc

WORKDIR /pintos
COPY . /pintos

# Pintos source root: src/ if present, otherwise the repository root.
RUN if [ -f src/Make.config ]; then ln -s /pintos/src /pintos-src; else ln -s /pintos /pintos-src; fi \
 && if [ -f /pintos-src/utils/Makefile ]; then make -C /pintos-src/utils; fi

ENV PATH="/pintos-src/utils:${PATH}"

# Run every suite under QEMU. Stock Pintos sends project 1 to Bochs, but the
# bochsrc that utils/pintos writes is rejected by current Bochs releases.
# Override with `-e SIMULATOR=--bochs` if a compatible Bochs is installed.
ENV SIMULATOR=--qemu

ENTRYPOINT ["/pintos/scripts/run-tests.sh"]
