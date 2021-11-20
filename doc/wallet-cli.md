# Raptoreum CLI wallet
It is possible to perform all wallet operations through command line.

This guide illustrates how to use the Raptoreum wallet through the command line interface.

## Syntax
```bash
./raptoreum-cli [options] <command> [params]
```

Example: 
```bash
./raptoreum-cli getwalletinfo
```

## Running
Go to you setup directory.

Run the Raptoreum daemon:
```bash
./raptoreumd
```

In a separate terminal window, run the commands detailed by this guide.

## Backup and import
### Backup
One of the first steps you should do once you create a wallet is to back it up. 

To backup your wallet use: 
```bash
./raptoreum-cli backupwallet "destination"
```
Where `destination` is either a directory or a path with filename. If destination is a directory, a file named `wallet.dat` will be generated at the destination directory. Make sure to save this file to a secure location.

### Import
To import a wallet from a backup, use: 
```bash
./raptoreum-cli importwallet "filename"
```
Where `filename` points to a wallet backup / dump file.

## Encryption
It is important to encrypt your wallet to avoid theft.

### Encrypting your wallet
To encrypt your wallet run the following command:
```bash
./raptoreum-cli encryptwallet "Your_Super_Strong_Passphrase"
```

Note that the passphrase should be at least one character long, but should be long to avoid bruteforce.

Once this command is executed, you will need to restart your Raptoreum daemon. 

### Running commands on an encrypted wallet
Any command using private keys (sending or signing) on an encrypted wallet requires a passphrase.
To use a command on an encrypted wallet, you should:
1. Use the `walletpassphrase` command to unlock the wallet
2. Run your command
3. Lock the wallet again using `walletlock`.

Example: 
```bash
./raptoreum-cli walletpassphrase "Your_Super_Strong_Passphrase"
# Run any command you wish
./raptoreum-cli signmessage "address" "test message"
# Lock the wallet again
./raptoreum-cli walletlock
```

### Changing passphrase
To change the wallet's passphrase: 
```bash
./raptoreum-cli walletpassphrasechange "oldpassphrase" "newpassphrase"
```

## Basic wallet operations
### Show balance
Raptoreum wallet holds different balances:
1. Confirmed balance (or just balance): calculated based on transactions having at least 1 confirmation.
2. Unconfirmed balance: calculated based on transactions that do not have enough confirmations yet.

To show confirmed balance: 
```bash
./raptoreum-cli getbalance
```

Optionally, you can specify the minimum number of confirmations to calculate the balance. The following command includes transactions with at least 6 blocks confirmed:
```bash
./raptoreum-cli getbalance "" 6
```

To show unconfirmed balance:
```bash
./raptoreum-cli getunconfirmedbalance
```

### Send RTM
To send RTM use the following command where "address" is the destination address and amount is the amount to be sent in RTM:
```bash
./raptoreum-cli sendtoaddress "address" amount
```

Optionally, it is possible to specify the following parameters:
1. "comment" (string): A comment describing the transaction.
2. "comment_to" (string): A comment to describe the organization or person you are sending the transaction to.
3. subtractfeefromamount (boolean): "true" to subtract the fees from the specified amount. By default this is set to "false".


### Address management
#### Generating a new address
In order to receive RTM, you should have an address.

To create a new address:
```bash
./raptoreum-cli getnewaddress
```

#### Listing balances by address
You can list you balance by address using:
```bash
./raptoreum-cli listaddressbalances
```

### List received RTM by address
To list the amount of RTM received by address:
```bash
./raptoreum-cli listreceivedbyaddress
```

By default, this will include transactions with at least 1 block confirmed. You can also specify the minimum number of confirmations for the calculation. For example the following command only uses transactions with at least 6 blocks confirmed:
```bash
./raptoreum-cli listreceivedbyaddress 6
```

### List transactions
The `listtransactions` command allows you to see your transactions. 

By default it would list the 10 most recent transactions: 
```bash
./raptoreum-cli listtransactions
```

It is possible to specify the number of transactions to return and an offset. For example, to return the transactions 100 to 120:
```bash
./raptoreum-cli listtransactions "*" 20 100
```


## Command line help
As this guide is only intended to illustrate the most common use cases, more advanced use cases are not described here. For further details on the available CLI commands, run:
```bash
./raptoreum-cli help
```
This lists all the available commands by section.

Then you can use `help <command>` for more details about a specific command. For example:
```bash
./raptoreum-cli help sendtoaddress
```

