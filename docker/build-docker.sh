#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-raptoreum/raptoreumd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

BUILD_DIR=${BUILD_DIR:-.}

rm docker/bin/*
mkdir docker/bin
cp $BUILD_DIR/src/raptoreumd docker/bin/
cp $BUILD_DIR/src/raptoreum-cli docker/bin/
cp $BUILD_DIR/src/raptoreum-tx docker/bin/
strip docker/bin/raptoreumd
strip docker/bin/raptoreum-cli
strip docker/bin/raptoreum-tx

docker build --pull -t $DOCKER_IMAGE:$DOCKER_TAG -f docker/Dockerfile docker
