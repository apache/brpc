# A image for building/testing brpc
FROM ubuntu:16.04

# prepare env
RUN apt-get update && apt-get install -y --no-install-recommends \
        curl \
        apt-utils \
        openssl \
        ca-certificates

# install deps
RUN apt-get update && apt-get install -y --no-install-recommends \
        git \
        g++ \
        make \
        libssl-dev \
        libgflags-dev \
        libprotobuf-dev \
        libprotoc-dev \
        protobuf-compiler \
        libleveldb-dev \
        libsnappy-dev && \
        apt-get clean -y

RUN git clone https://github.com/brpc/brpc.git brpc
RUN cd /brpc && sh config_brpc.sh --headers=/usr/include --libs=/usr/lib && \
    make -j "$(nproc)"
