FROM debian:latest

WORKDIR /ws

ARG DEBIAN_FRONTEND=noninteractive
ARG COMPILER_ARCH
ENV TZ=UTC
ENV DISTCC_HOSTS=127.0.0.1
ENV DISTCC_IO_TIMEOUT=960

RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && \
    echo $TZ > /etc/timezone && \
    apt-get update && \
    apt-get install -y git distcc cmake ninja-build python3 file g++

RUN mkdir -p /opt/bin/distcc_symlinks && \
    ln -s /usr/bin/distcc /opt/bin/distcc_symlinks/$COMPILER_ARCH-gcc &&\
    ln -s /usr/bin/distcc /opt/bin/distcc_symlinks/$COMPILER_ARCH-g++

ENV PATH=/opt/bin/distcc_symlinks:$PATH

COPY . .

RUN cp ./test_patch_do_not_upstream/simd128.h ./third_party/simde/simde/wasm/simd128.h && \
    WASM2C_CFLAGS=-DWASM_RT_USE_STACK_DEPTH_COUNT=1 CC=$COMPILER_ARCH-gcc CXX=$COMPILER_ARCH-g++ cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DTEST_TIMEOUT=$((120*8)) && \
    cmake --build build
