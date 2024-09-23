#!/usr/bin/env bash

export LC_ALL=C

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $DIR/.. || exit

DOCKER_IMAGE=${DOCKER_IMAGE:-raptoreum/raptoreumd-develop}
DOCKER_TAG=${DOCKER_TAG:-latest}

if [ -n "$DOCKER_REPO" ]; then
  DOCKER_IMAGE_WITH_REPO=$DOCKER_REPO/$DOCKER_IMAGE
else
  DOCKER_IMAGE_WITH_REPO=$DOCKER_IMAGE
fi

docker tag $DOCKER_IMAGE:$DOCKER_TAG $DOCKER_IMAGE_WITH_REPO:$DOCKER_TAG
docker push $DOCKER_IMAGE_WITH_REPO:$DOCKER_TAG
docker rmi $DOCKER_IMAGE_WITH_REPO:$DOCKER_TAG
