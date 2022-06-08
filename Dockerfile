FROM --platform=linux/amd64 ubuntu:20.04 as builder

RUN apt-get update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential gcc-multilib

ADD . /fcc
WORKDIR /fcc
RUN make

RUN mkdir -p /deps
RUN ldd /fcc/bin/release/fcc | tr -s '[:blank:]' '\n' | grep '^/' | xargs -I % sh -c 'cp % /deps;'

FROM ubuntu:20.04 as package

COPY --from=builder /deps /deps
COPY --from=builder /fcc/bin/release/fcc /fcc/bin/release/fcc
ENV LD_LIBRARY_PATH=/deps

