FROM ubuntu:20.04
LABEL maintainer="Raptoreum Developers <dev@raptoreum.org>"
LABEL description="RaptoreumCore testnet Docker Image"

ARG USER_ID
ARG GROUP_ID
ARG VER

ENV HOME /app

# Add user with specified (or default) user/group ids
ENV USER_ID ${USER_ID:-1000}
ENV GROUP_ID ${GROUP_ID:-1000}
RUN groupadd -g ${GROUP_ID} raptoreum \
&&  useradd -u ${USER_ID} -g raptoreum -s /bin/bash -m -d /app raptoreum \
&&  mkdir /app/.raptoreumcore \
&&  chown raptoreum:raptoreum -R /app

RUN apt update \
&&  apt -y install --no-install-recommends \
    ca-certificates \
	unzip \
    curl \
&&  rm -rf /var/lib/apt/lists/*

COPY entrypoint.sh testnet-raptoreum.conf /app/

RUN mach=$(uname -m) \
&&  case $mach in x86_64) arch="ubuntu20_64"; ;; *) echo "ERROR: Machine type $mach not supported."; ;; esac \
&&  curl -L https://github.com/Raptor3um/raptoreum/releases/download/${VER}/raptoreum_${VER}_$arch.tar.gz | tar -xvz --strip-components=1 -C /usr/local/bin \
&&  chmod a+x /usr/local/bin/* /app/entrypoint.sh

USER raptoreum

VOLUME ["/app/.raptoreumcore"]

EXPOSE 10227 19998

WORKDIR /app

CMD ["/app/entrypoint.sh"]
