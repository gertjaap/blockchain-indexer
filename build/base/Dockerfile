FROM ubuntu:16.04

RUN apt-get update && apt install -y git wget build-essential libleveldb-dev cmake automake libssl-dev libtool autoconf libjsonrpccpp-dev libjsoncpp-dev libcurl4-openssl-dev

RUN mkdir /src
WORKDIR /src
RUN git clone --recursive https://github.com/corvusoft/restbed.git
RUN mkdir restbed/build
WORKDIR restbed/build
RUN cmake -DBUILD_SSL=NO -DBUILD_TESTS=NO ..
RUN make install
RUN cp -r ../distribution/include/* /usr/local/include
RUN cp -r ../distribution/lib/* /usr/lib

WORKDIR /
RUN git clone https://github.com/vertcoin/vertcoin
WORKDIR /vertcoin/src/secp256k1
RUN ./autogen.sh
RUN ./configure
RUN make
RUN make install 

RUN ldconfig
