0.12.0 Release notes
====================


Raptoreum Core version 0.12.0 is now available from:

  https://raptoreum.io/downloads

Please report bugs using the issue tracker at github:

  https://github.com/raptor3um/raptoreum/issues


How to Upgrade
--------------

If you are running an older version, shut it down. Wait until it has completely
shut down (which might take a few minutes for older versions), then run the
installer (on Windows) or just copy over /Applications/Raptoreum-Qt (on Mac) or
raptoreumd/raptoreum-qt (on Linux).

**This new version uses transaction indexing by default, you will need to reindex 
the blockchain. To do so, start the client with --reindex.**


Downgrade warning
------------------

Because release 0.12.0 and later makes use of headers-first synchronization and
parallel block download (see further), the block files and databases are not
backwards-compatible with pre-0.12 versions of Raptoreum Core or other software:

* Blocks will be stored on disk out of order (in the order they are
received, really), which makes it incompatible with some tools or
other programs. Reindexing using earlier versions will also not work
anymore as a result of this.

* The block index database will now hold headers for which no block is
stored on disk, which earlier versions won't support.

If you want to be able to downgrade smoothly, make a backup of your entire data
directory. Without this your node will need start syncing (or importing from
bootstrap.dat) anew afterwards. It is possible that the data from a completely
synchronised 0.10 node may be usable in older versions as-is, but this is not
supported and may break as soon as the older version attempts to reindex.

This does not affect wallet forward or backward compatibility.


0.12.0 changelog
----------------

Switched to Bitcoin Core version 0.10 - https://bitcoin.org/en/release/v0.10.0
- Implemented decentralized budget system 
- Removed reference node
- Implemented new decentralized smartnode payment consensus system
- Improved speed of DS
- New smartnode payment/winners/budgets syncing strategy
- Platform independent smartnode ranking system
- Smartnode broadcasts, pings and winners now use the inventory system
- Transaction indexing is enabled by default for all clients
- Better implementation of IX block reprocessing to find and remove an invalid block
- IX nearly 100% successful with new implementation
- Lots of UI improvements and fixes including DS progress, wallet repair buttons, UI settings/filters persistence etc


Credits
--------

Thanks to who contributed to this release, at least:

eduffield  
UdjinM6  
Crowning  
moli  
flare  
thelazier  
adios  
poiuty  
scratchy  
moocowmoo  
the-baker  
BrainShutdown  
Lukas_Jackson  
Sub-Ether  
italx  
yidakee  
TanteStefana  
coingun  
tungfa  
MangledBlue  
AjM  
Lariondos  
elbereth  
minersday  
qwizzie  
TaoOfSatoshi  
dark-sailor  
AlexMomo  
snogcel
bertlebbert

As well as everyone that helped translating on [Transifex](https://www.transifex.com/projects/p/raptoreum/).
