# PiEconetBridge, userspace only -- no Pi, no Econet HAT, no kernel module.
#
# The upstream project builds its userspace bridge from a make target that is
# independent of the kernel module (`build-utilities` vs `build-module`; only
# the latter needs kernel headers). The kernel module is reached only through a
# `WIRE NET n ON DEVICE /dev/econet-gpio` configuration line; omit it and the
# configured networks become upstream's "Null" networks, which carry local
# emulation and distant AUN hosts only. That is exactly the topology Beebium's
# AUN interop tests need.
#
# Deliberately NOT `make install`: that path wants systemd, udev rules, a
# system group and /etc/econet-gpio. We want one binary and one config file.
#
# See docs/discussion/pieconetbridge-aun-interop-testing.md.

# The upstream commit under test. Bumping this is a deliberate change: read the
# upstream log for protocol-affecting changes first, and re-run the interop
# suite. This is the same revision used for the Piconet wire investigation in
# docs/discussion/pieconetbridge-investigation.md, so findings stay comparable.
ARG PIEB_COMMIT=57243a151184f9d570de7d40c98c61a2779c3053

FROM ubuntu:24.04 AS builder
ARG PIEB_COMMIT

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        gcc \
        git \
        libc6-dev \
        libssl-dev \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
RUN git clone --no-checkout https://github.com/cr12925/PiEconetBridge.git . \
    && git checkout --detach "${PIEB_COMMIT}"

# `.noexplain` selects the link line that omits libexplain, which is packaged
# only for some distributions and contributes nothing here beyond prettier
# strerror output.
RUN touch utilities/.noexplain \
    && make build-utilities

FROM ubuntu:24.04
ARG PIEB_COMMIT

# netcat is not needed by the bridge. It is here for the test fixture's
# address-discovery probe: on Docker Desktop the address a container's traffic
# appears to come from is neither the published-port address nor anything the
# container can introspect, and Beebium's AUN backend drops datagrams from
# senders that are not in its peer table. The fixture therefore has the
# container send one datagram to a host-side socket and reads the source
# address off it. See pieb_test_support.bridge.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3t64 \
        netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/utilities/econet-hpbridge /usr/local/sbin/econet-hpbridge
COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

# The bridge reads its configuration from a fixed path. The entrypoint renders
# it there from the environment so a test can vary the topology without
# rebuilding the image.
RUN mkdir -p /etc/econet-gpio /filestore

LABEL org.opencontainers.image.title="PiEconetBridge (userspace)" \
      org.opencontainers.image.description="Wire-free PiEconetBridge for Beebium AUN interop tests" \
      org.opencontainers.image.source="https://github.com/cr12925/PiEconetBridge" \
      uk.org.beebium.pieb-commit="${PIEB_COMMIT}"

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
