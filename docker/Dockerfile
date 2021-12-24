FROM debian:stretch
LABEL maintainer="Raptoreum Developers <dev@raptoreum.org>"
LABEL description="Dockerised RaptoreumCore, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
