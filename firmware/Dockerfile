FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

# System deps
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    build-essential cmake git pkg-config \
    flex bison libfmt-dev libxml2-dev libboost-all-dev \
    can-utils && \
    update-ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# Workdir where we’ll mount the repo
WORKDIR /workspace

# We do NOT copy the repo or build at image build time.
# docker-compose will mount the repo and run the build command.
CMD [ "bash" ]
