FROM vtc-wallet-middleware-base

RUN apt-get update

ADD src /root/sources/vtc-wallet-middleware-cpp/src
ADD Makefile /root/sources/vtc-wallet-middleware-cpp/Makefile

RUN make -C /root/sources/vtc-wallet-middleware-cpp


