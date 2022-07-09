#!/bin/sh

export LC_ALL=C
set -eu

uname_S=$(uname -s 2>/dev/null || echo not)

if [ "$uname_S" = "Darwin" ]; then
    PARAMS_DIR="$HOME/Library/Application Support/RaptoreumParams"
else
    PARAMS_DIR="$HOME/.raptoreum-params"
fi

# Commented out because these are unused; see below.
#SPROUT_PKEY_NAME='sprout-proving.key'
#SPROUT_VKEY_NAME='sprout-verifying.key'
SAPLING_SPEND_NAME='sapling-spend.params'
SAPLING_OUTPUT_NAME='sapling-output.params'
SAPLING_SPROUT_GROTH16_NAME='sprout-groth16.params'
DOWNLOAD_URL="https://github.com/Raptor3um/raptoreum/releases/tag/1.2.15.3"
#IPFS_HASH="/ipfs/QmXRHVGLQBiKwvNq7c2vPxAKz1zRVmMYbmt7G5TQss7tY7"

SHA256CMD="$(command -v sha256sum || echo shasum)"
SHA256ARGS="$(command -v sha256sum >/dev/null || echo \"-a 256\")"

WGETCMD="$(command -v wget || echo '')"
IPFSCMD="$(command -v ipfs || echo '')"
CURLCMD="$(command -v curl || echo '')"

# fetch methods can be disabled with RTM_DISABLE_SOMETHING=1
RTM_DISABLE_WGET="${RTM_DISABLE_WGET:-}"
#RTM_DISABLE_IPFS="${RTM_DISABLE_IPFS:-}"
RTM_DISABLE_CURL="${RTM_DISABLE_CURL:-}"

LOCKFILE=/tmp/fetch_params.lock

fetch_wget() {
    if [ -z "$WGETCMD" ] || ! [ -z "$RTM_DISABLE_WGET" ]; then
        return 1
    fi

    cat <<EOF

Retrieving (wget): $DOWNLOAD_URL/$1
EOF

    wget \
        --progress=dot:giga \
        --output-document="$2" \
        --continue \
        --retry-connrefused --waitretry=3 --timeout=30 \
        "$DOWNLOAD_URL/$1"
}

#fetch_ipfs() {
#    if [ -z "$IPFSCMD" ] || ! [ -z "$RTM_DISABLE_IPFS" ]; then
#        return 1
#    fi

#    cat <<EOF

#Retrieving (ipfs): $IPFS_HASH/$1
#EOF

#    ipfs get --output "$2" "$IPFS_HASH/$1"
#}

fetch_curl() {
    if [ -z "$CURLCMD" ] || ! [ -z "$RTM_DISABLE_CURL" ]; then
        return 1
    fi

    cat <<EOF

Retrieving (curl): $DOWNLOAD_URL/$1
EOF

    curl \
        --output "$2" \
        -# -L -C - \
        "$DOWNLOAD_URL/$1"

}

fetch_failure() {
    cat >&2 <<EOF

Failed to fetch the Raptoreum parameters!
Try installing one of the following programs and make sure you're online:

# * ipfs
 * wget
 * curl

EOF
    exit 1
}

fetch_params() {
    # We only set these variables inside this function,
    # and unset them at the end of the function.
    filename="$1"
    output="$2"
    dlname="${output}.dl"
    expectedhash="$3"

    if ! [ -f "$output" ]
    then
        for i in 1 2
        do
            for method in wget ipfs curl failure; do
                if "fetch_$method" "${filename}.part.${i}" "${dlname}.part.${i}"; then
                    echo "Download of part ${i} successful!"
                    break
                fi
            done
        done

        for i in 1 2
        do
            if ! [ -f "${dlname}.part.${i}" ]
            then
                fetch_failure
            fi
        done

        cat "${dlname}.part.1" "${dlname}.part.2" > "${dlname}"
        rm "${dlname}.part.1" "${dlname}.part.2"

        "$SHA256CMD" $SHA256ARGS -c <<EOF
$expectedhash  $dlname
EOF

        # Check the exit code of the shasum command:
        CHECKSUM_RESULT=$?
        if [ $CHECKSUM_RESULT -eq 0 ]; then
            mv -v "$dlname" "$output"
        else
            echo "Failed to verify parameter checksums!" >&2
            exit 1
        fi
    fi

    unset -v filename
    unset -v output
    unset -v dlname
    unset -v expectedhash
}

# Use flock to prevent parallel execution.
lock() {
    if [ "$uname_S" = "Darwin" ]; then
        if shlock -f ${LOCKFILE} -p $$; then
            return 0
        else
            return 1
        fi
    else
        # create lock file
        eval "exec 9>$LOCKFILE"
        # acquire the lock
        flock -n 9 \
            && return 0 \
            || return 1
    fi
}

exit_locked_error() {
    echo "Only one instance of fetch-params.sh can be run at a time." >&2
    exit 1
}

main() {

    lock fetch-params.sh \
    || exit_locked_error

    cat <<EOF
RTM - fetch-params.sh

This script will fetch the Raptoreum SNARK parameters and verify their
integrity with sha256sum.

If they already exist locally, it will exit now and do nothing else.
EOF

    # Now create PARAMS_DIR and insert a README if necessary:
    if ! [ -d "$PARAMS_DIR" ]
    then
        mkdir -p "$PARAMS_DIR"
        README_PATH="$PARAMS_DIR/README"
        cat >> "$README_PATH" <<EOF
This directory stores common Raptoreum SNARK parameters. Note that it is
distinct from the daemon's -datadir argument because the parameters are
large and may be shared across multiple distinct -datadir's such as when
setting up test networks.
EOF

        # This may be the first time the user's run this script, so give
        # them some info, especially about bandwidth usage:
        cat <<EOF
The complete parameters are currently just under 1GB in size, so plan 
accordingly for your bandwidth constraints. If the Sprout parameters are
already present the additional Sapling parameters required are just under 
800MB in size. If the files are already present and have the correct 
sha256sum, no networking is used.

Creating params directory. For details about this directory, see:
$README_PATH

EOF
    fi

    cd "$PARAMS_DIR"

    # Sprout parameters:
    # Commented out because they are unneeded, but we will eventually update
    # this to delete the parameters if possible.
    #fetch_params "$SPROUT_PKEY_NAME" "$PARAMS_DIR/$SPROUT_PKEY_NAME" "8bc20a7f013b2b58970cddd2e7ea028975c88ae7ceb9259a5344a16bc2c0eef7"
    #fetch_params "$SPROUT_VKEY_NAME" "$PARAMS_DIR/$SPROUT_VKEY_NAME" "4bd498dae0aacfd8e98dc306338d017d9c08dd0918ead18172bd0aec2fc5df82"

    # Sapling parameters:
    #fetch_params "$SAPLING_SPEND_NAME" "$PARAMS_DIR/$SAPLING_SPEND_NAME" "8e48ffd23abb3a5fd9c5589204f32d9c31285a04b78096ba40a79b75677efc13"
    #fetch_params "$SAPLING_OUTPUT_NAME" "$PARAMS_DIR/$SAPLING_OUTPUT_NAME" "2f0ebbcbb9bb0bcffe95a397e7eba89c29eb4dde6191c339db88570e3f3fb0e4"
    #fetch_params "$SAPLING_SPROUT_GROTH16_NAME" "$PARAMS_DIR/$SAPLING_SPROUT_GROTH16_NAME" "b685d700c60328498fbde589c8c7c484c722b788b265b72af448a5bf0ee55b50"
}

main
rm -f $LOCKFILE
exit 0
