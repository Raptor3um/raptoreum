# Docker Images for Raptoreum

These files provide Docker images to automatically bootstrap and start raptoreumd.  The blockchain and wallet data are kept
in separate docker volumes, so the containers can be stopped, updated, and restarted without affecting the data.

## Configuring Options

### raptoreumd Options:

Note that these images do **not** create a wallet by default.  You can enable this, but you are responsible for keeping backups of the wallet.dat file.

***Deleting the docker volume will delete the wallet***

Before building the images, review the initial options in `mainnet-raptoreum.conf` and/or  `testnet-raptoreum.conf`.

You may want to change options (in particular, enalbing the wallet, adding indices, and additional nodes for the testnet).

### Version:

The versions of the released images are defined  in `.env` - update these if you wish to use a different version.

## Creating the Images

To create the images (or recreate if the `.conf` files are changed), use the following commands:

* **mainnet**: `docker-compose -f docker-compose-mainnet.yaml build`
* **testnet**: `docker-compose -f docker-compose-testnet.yaml build`

## Starting the Containers

To start a container and monitor the output:
* **mainnet**: `docker-compose -f docker-compose-mainnet.yaml -p mainnet up`
* **testnet**: `docker-compose -f docker-compose-testnet.yaml -p testnet up`

To start a container and leave it running in the background (detached):
* **mainnet**: `docker-compose -f docker-compose-mainnet.yaml -p mainnet up -d`
* **testnet**: `docker-compose -f docker-compose-testnet.yaml -p testnet up -d`

## Stopping the Containers

To stop a running container:
* **mainnet**: `docker-compose -f docker-compose-mainnet.yaml -p mainnet down`
* **testnet**: `docker-compose -f docker-compose-testnet.yaml -p testnet down`

## Resyncing the Blockchain

The containers automatically download and install the bootstrap when starting.  You can force it to delete the working blockchain (blocks, chainstate, database, evodb, and llmq directories), which will cause it to restart from the bootstrap.  Additionally, you can tell it to remove the bootstrap zip file, which will force it to download a fresh bootstrap before starting.

To reset the chain data, edit the `docker-compose-mainnet.yaml` or `docker-compose-testnet.yaml` and adjust the following two lines as desired:

```ini
# Enable the following line to remove existing chain data and re-bootstrap:
#reset_chain: 1

# Enable the following line to delete any existing bootstrap.zip file (to force a fresh download):
#reset_bootstrap: 1
```

When you start the containers, they will use these settings and reset as desired.

NB: If you leave these settings active, they will resync **every** time to start the containers, so it is recommended to turn these off after forcing the resync.

## Running the container interactively

There are two ways to run the container interactively - with raptoreumd automatically started or fully manual.

### Interacting with a running container

If the container is running, you can jump into the container and issue commands locally.

To get a bash prompt inside the container:
* **mainnet**: `docker exec -it rtm-mainnet-node bash`
* **testnet**: `docker exec -it rtm-testnet-node bash`

Use `exit` when done.

### Starting the container manually

This can be useful if you want to manually start raptoreumd and work with it.

* **mainnet**: `docker-compose -f docker-compose-mainnet.yaml -p mainnet run rtm-mainnet-node bash`
* **testnet**: `docker-compose -f docker-compose-testnet.yaml -p testnet run rtm-testnet-node bash`
