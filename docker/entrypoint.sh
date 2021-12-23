#!/bin/bash

# Environment Variables for Startup:
# reset_chain     - If not zero, will delete blockchain data if it exists (useful with bootstrap_url/bootstrap_file to resync)
# reset_bootstrap - If not zero, will delete the previously downloaded bootstrap.zip file - this is to force a re-download of the bootstrap.
# bootstrap_file  - Path to bootstrap zip file within container (useful for quick reloads).
# bootstrap_url   - If provided and blockchain data is not present, it will download and install before starting the daemon

# Set default values for envionrment variables if not provided on the command line:
: "${reset_chain:=0}"
: "${reset_bootstrap:=0}"

export LC_ALL=C
set -e

# Get Tor service IP if running
if [ "$1" == "raptoreumd" ]; then
  # Because raptoreumd only accept torcontrol= host as an ip only, we resolve it here and add to config
  if [[ "$TOR_CONTROL_HOST" ]] && [[ "$TOR_CONTROL_PORT" ]] && [[ "$TOR_PROXY_PORT" ]]; then
    TOR_IP=$(getent hosts $TOR_CONTROL_HOST | cut -d ' ' -f 1)
    echo "proxy=$TOR_IP:$TOR_PROXY_PORT" >> "$HOME/.raptoreumcore/raptoreum.conf"
    echo "Added "proxy=$TOR_IP:$TOR_PROXY_PORT" to $HOME/.raptoreumcore/raptoreum.conf"
    echo "torcontrol=$TOR_IP:$TOR_CONTROL_PORT" >> "$HOME/.raptoreumcore/raptoreum.conf"
    echo "Added "torcontrol=$TOR_IP:$TOR_CONTROL_PORT" to $HOME/.raptoreumcore/raptoreum.conf"
    echo -e "\n"
  else
    echo "Tor control credentials not provided"
  fi
fi

if [ $reset_chain -ne 0 ]; then
   echo "Clearing blockchain data"
   rm -rf /app/.raptoreumcore/blocks /app/.raptoreumcore/chainstate /app/.raptoreumcore/evodb /app/.raptoreumcore/llmq
fi

if [ $reset_bootstrap -ne 0 ]; then
   echo "Removing bootstrap file: ${bootstrap_file}"
   rm -f "${bootstrap_file}"
fi

if [ ! -d /app/.raptoreumcore/blocks ]; then
   echo "Blockchain data not found"

   if [ ! -z "${bootstrap_file}" ] && [ -f "${bootstrap_file}" ] ; then
      echo "Loading bootstrap file: ${bootstrap_file}"
      unzip -o "${bootstrap_file}" -d /app/.raptoreumcore
   else
      if [ ! -z "${bootstrap_url}" ]; then
         echo "Downloading bootstrap file from: ${bootstrap_url}"
         curl -L "${bootstrap_url}" > ${bootstrap_file}
         unzip -o "${bootstrap_file}" -d /app/.raptoreumcore
      fi
   fi
   # HACK:  Remove this after the embedded directory is removed from the testnet zip file
   if [ -d /app/.raptoreumcore/Testnet_Bootstrap ]; then
      echo "Moving TestNet_Bootstrap to the correct location"
      mv /app/.raptoreumcore/Testnet_Bootstrap/* /app/.raptoreumcore/
      rm -rf /app/.raptoreumcore/Testnet_Bootstrap
   fi
fi

# if raptoreum.conf does not exist, use our reference:
if [ ! -f /app/.raptoreumcore/raptoreum.conf ]; then
   echo "Creating raptoreum.conf from reference"
   cp ${config_template_file} /app/.raptoreumcore/raptoreum.conf
fi

if [ "$#" -eq 0 ]; then
   echo "No additional commands provided, exiting"
else
    exec "$@"
fi
