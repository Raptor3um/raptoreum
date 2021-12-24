#!/bin/bash
# use testnet settings,  if you need mainnet,  use ~/.raptoreumcore/raptoreumd.pid file instead
raptoreum_pid=$(<~/.raptoreumcore/testnet3/raptoreumd.pid)
sudo gdb -batch -ex "source debug.gdb" raptoreumd ${raptoreum_pid}
