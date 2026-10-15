# Historical toolchain used for this port; no claim of current EDK II support.
FROM ubuntu:16.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential uuid-dev git nasm python python3 bison flex iasl \
    ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /workspace
CMD ["/bin/bash"]
