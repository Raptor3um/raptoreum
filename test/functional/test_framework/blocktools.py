#!/usr/bin/env python3
# Copyright (c) 2015-2016 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Utilities for manipulating blocks and transactions."""

import struct

from .mininode import *
from .messages import CFinalCommitment, CFinalCommitmentTxPayload
from .script import CScript, OP_TRUE, OP_CHECKSIG, OP_DUP, OP_HASH160, OP_EQUALVERIFY

# Create a block (with regtest difficulty)
#
# Pass `node` for a block that will actually be submitted: Raptoreum wants a
# (possibly null) quorum commitment on every block inside a DKG mining window
# and otherwise answers "bad-qc-missing". The height comes back out of the
# coinbase's CbTx payload, so callers only have to supply the node.
def create_block(hashprev, coinbase, nTime=None, node=None, block_hashes=None):
    block = CBlock()
    if nTime is None:
        import time
        block.nTime = int(time.time()+600)
    else:
        block.nTime = nTime
    block.hashPrevBlock = hashprev
    block.nBits = 0x207fffff # Will break after a difficulty adjustment...
    block.vtx.append(coinbase)
    if node is not None:
        block.vtx.extend(create_quorum_commitments(
            node, coinbase_height(coinbase), block_hashes))
    block.hashMerkleRoot = block.calc_merkle_root()
    block.calc_sha256()
    return block

def coinbase_height(coinbase):
    """Height out of a CbTx payload: uint16 version, then uint32 height."""
    return struct.unpack("<I", coinbase.vExtraPayload[2:6])[0]

def serialize_script_num(value):
    r = bytearray(0)
    if value == 0:
        return r
    neg = value < 0
    absvalue = -value if neg else value
    while (absvalue):
        r.append(int(absvalue & 0xff))
        absvalue >>= 8
    if r[-1] & 0x80:
        r.append(0x80 if neg else 0)
    elif neg:
        r[-1] |= 0x80
    return r

# (llmqType, size, dkgInterval, miningWindowStart, miningWindowEnd) from
# llmq/quorums_parameters.h. Every block inside a type's window needs a commitment
# for it, null or not, until a real one is mined (IsCommitmentRequired). Sizes are
# the defaults; a test that resizes with -llmqtestparams and hand-builds blocks
# would need its own.
REGTEST_LLMQS = {
    "llmq_test":     (100, 3, 30, 10, 18),
    "llmq_test_v17": (101, 3, 30, 10, 18),
}

def create_null_commitment(llmq_type, size, height, quorum_hash):
    qc = CFinalCommitment()
    qc.nVersion = 1          # quorumUpdateVotes is only on the wire above 1
    qc.llmqType = llmq_type
    qc.quorumHash = quorum_hash
    qc.signers = [False] * size
    qc.validMembers = [False] * size

    payload = CFinalCommitmentTxPayload()
    payload.nVersion = 1
    payload.nHeight = height
    payload.commitment = qc

    tx = CTransaction()
    tx.nVersion = 3
    tx.nType = 6                       # TRANSACTION_QUORUM_COMMITMENT
    tx.vExtraPayload = payload.serialize()
    tx.calc_sha256()
    return tx

def create_quorum_commitments(node, height, block_hashes=None):
    """The commitments a block at `height` must carry. The node's own miner puts
    these in every block it builds; a hand-built block has to do it itself.

    A commitment is emitted for every type whose mining window covers `height`,
    without asking the node which types are currently enabled. That is both
    simpler and more correct for a test that builds a run of blocks before
    sending them: llmq_test_v17 turns on partway up the chain when the v17
    deployment activates, so the answer at build time is not the answer at
    validation time. Emitting one early is harmless -- ProcessBlock only applies
    its "bad-qc-not-allowed" rule to enabled types, and ProcessCommitment returns
    as soon as it sees a null commitment whose quorum hash and sizes check out.

    `block_hashes` covers blocks the caller has built but not yet sent, which the
    node therefore cannot look up: either a {height: sha256} map, or a callable
    taking a height and returning a hash or None. A test that builds competing
    chains needs the callable -- a flat map keeps only one block per height, and
    picking the wrong fork's block earns bad-qc-quorum-hash.
    """
    txs = []
    for llmq_type, size, interval, start, end in REGTEST_LLMQS.values():
        phase = height % interval
        if not (start <= phase <= end):
            continue
        # GetQuorumBlockHash: the block that started this DKG session.
        session_height = height - phase
        quorum_hash = None
        if block_hashes is not None:
            quorum_hash = (block_hashes(session_height) if callable(block_hashes)
                           else block_hashes.get(session_height))
        if quorum_hash is None:
            quorum_hash = int(node.getblockhash(session_height), 16)
        txs.append(create_null_commitment(llmq_type, size, height, quorum_hash))
    return txs

# Founder fee on regtest: 5% forever from the block after startBlock 500. The
# hash160 is that address decoded. Blocks above the start height are rejected
# with bad-cb-founder-payment-not-found unless the coinbase pays it.
FOUNDER_START_HEIGHT = 500
FOUNDER_REWARD_PERCENT = 5
FOUNDER_SCRIPT = CScript([OP_DUP, OP_HASH160,
                          bytes.fromhex("9c7045db5557d94a867ab684e23124d710fd44e3"),
                          OP_EQUALVERIFY, OP_CHECKSIG])

def get_block_subsidy(height):
    """Mirror of GetBlockSubsidy() in src/validation.cpp, which takes the
    PREVIOUS block's height. Regtest never reaches the reductions that start at
    553532, so only the launch window and the flat 5000 matter here."""
    return (4 if height - 1 < 720 else 5000) * COIN

def get_founder_payment(height, subsidy):
    if height <= FOUNDER_START_HEIGHT:
        return 0
    return subsidy * FOUNDER_REWARD_PERCENT // 100

# Create a coinbase transaction, assuming no miner fees.
# If pubkey is passed in, the coinbase output will be a P2PK output;
# otherwise an anyone-can-spend output.
# dip4_activated defaults on: CRegTestParams sets DIP0003Enabled = true, so
# fDIP0003Active_context holds from height 1 and validation.cpp:4047 rejects any
# non-genesis coinbase that is not a CbTx with "bad-cb-type". Dash activates DIP3
# at a height and so defaults this off; Raptoreum never has a window where it is.
def create_coinbase(height, pubkey = None, dip4_activated=True):
    coinbase = CTransaction()
    coinbase.vin.append(CTxIn(COutPoint(0, 0xffffffff),
                ser_string(serialize_script_num(height)), 0xffffffff))
    subsidy = get_block_subsidy(height)
    founder = get_founder_payment(height, subsidy)
    coinbaseoutput = CTxOut()
    coinbaseoutput.nValue = subsidy - founder
    if (pubkey != None):
        coinbaseoutput.scriptPubKey = CScript([pubkey, OP_CHECKSIG])
    else:
        coinbaseoutput.scriptPubKey = CScript([OP_TRUE])
    coinbase.vout = [ coinbaseoutput ]
    if founder:
        coinbase.vout.append(CTxOut(founder, FOUNDER_SCRIPT))
    if dip4_activated:
        coinbase.nVersion = 3
        coinbase.nType = 5
        cbtx_payload = CCbTx(2, height, 0, 0)
        coinbase.vExtraPayload = cbtx_payload.serialize()
    coinbase.calc_sha256()
    return coinbase

# Create a transaction.
# If the scriptPubKey is not specified, make it anyone-can-spend.
def create_transaction(prevtx, n, sig, value, scriptPubKey=CScript()):
    tx = CTransaction()
    assert(n < len(prevtx.vout))
    tx.vin.append(CTxIn(COutPoint(prevtx.sha256, n), sig, 0xffffffff))
    tx.vout.append(CTxOut(value, scriptPubKey))
    tx.calc_sha256()
    return tx

def get_legacy_sigopcount_block(block, fAccurate=True):
    count = 0
    for tx in block.vtx:
        count += get_legacy_sigopcount_tx(tx, fAccurate)
    return count

def get_legacy_sigopcount_tx(tx, fAccurate=True):
    count = 0
    for i in tx.vout:
        count += i.scriptPubKey.GetSigOpCount(fAccurate)
    for j in tx.vin:
        # scriptSig might be of type bytes, so convert to CScript for the moment
        count += CScript(j.scriptSig).GetSigOpCount(fAccurate)
    return count

# GetSmartnodePayment: nothing until the list holds ten smartnodes, then the
# share from SmartnodeCollaterals, {{240, 0}, {INT_MAX, 20}}. Dash's realloc
# deployment has no counterpart here.
SMARTNODE_PAYMENT_START_HEIGHT = 240
SMARTNODE_PAYMENT_PERCENT = 20

def get_smartnode_payment(nHeight, blockValue, mn_count):
    if mn_count < 10:
        return 0
    if nHeight <= SMARTNODE_PAYMENT_START_HEIGHT:
        return 0
    return blockValue * SMARTNODE_PAYMENT_PERCENT // 100
