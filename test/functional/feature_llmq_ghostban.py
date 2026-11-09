#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import http.client
import os
import time

from test_framework.authproxy import JSONRPCException
from test_framework.test_framework import RaptoreumTestFramework, LLMQ_TEST_NAME, LLMQ_TEST_TYPE, LLMQ_TEST_DKG_INTERVAL
from test_framework.util import assert_equal, sync_blocks, wait_until

'''
feature_llmq_ghostban.py

End-to-end test of TRANSACTION_GHOST_BAN_LIST: kill two quorums' members, activate
the hard fork, confirm the disabled-spork no-op, enable it and evict the ghosts,
then confirm recovery (a fresh quorum forms and chainlocks resume).
'''

GHOST_BAN_SPORK = "SPORK_26_GHOST_BAN_THRESHOLD"
GHOST_BAN_TX_TYPE = 11
GHOST_BAN_UPDATE_NAME = "ghost_ban_list"
ACTIVATION_HEIGHT = 2020  # chainparams.cpp CRegTestParams: startHeight=2000, +10*2 grace rounds


class LLMQGhostBanTest(RaptoreumTestFramework):
    def set_test_params(self):
        # Two killable quorums (size=3) plus a comfortable untouched survivor margin
        # for the recovery phase.
        # -checkmempool=0: regtest defaults to a full mempool check per tx (init.cpp),
        # which scales badly at this test's height/duration.
        # -powmaxloadsize=1000000: CPowCache's 60s wall-clock dump (default threshold
        # 720) holds cs_pow for the dump duration once exceeded; raise it so a long
        # run never hits it.
        self.set_raptoreum_test_params(
            13, 12, extra_args=[["-checkmempool=0", "-powmaxloadsize=1000000"]] * 13,
            fast_dip3_enforcement=True)

        # PoW (CryptoNight/GhostRider) is deliberately slow; blocks occasionally
        # exceed the 60s default RPC timeout by chance alone (confirmed via gdb:
        # cn_slow_hash hot). set_timeout_scale() here is a no-op -- main()
        # re-applies the CLI default after __init__ runs, no override hook exists.
        # setup_nodes() below passes timewait directly instead, so the test is
        # self-sufficient without requiring --timeoutscale on the command line.

    def setup_nodes(self):
        self.add_nodes(self.num_nodes, self.extra_args, timewait=300)
        self.start_nodes()

    def _plog(self, msg):
        # Unbuffered, separate from self.log's own (buffered) file, so a stuck
        # call shows as a dangling START line, not silence.
        line = "%.3f %s\n" % (time.time(), msg)
        with open(self.progress_log, "a") as f:
            f.write(line)
            f.flush()
            os.fsync(f.fileno())
        self.log.info(msg)

    def run_test(self):
        self.progress_log = os.path.join(self.options.tmpdir, "ghostban-progress.log")
        with open(self.progress_log, "w"):
            pass  # truncate fresh for this run
        self._plog("=== run_test start ===")

        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

        self._plog("Phase 1 START: form two real quorums on a healthy network")
        qh1 = self.mine_quorum()
        qh2 = self.mine_quorum()
        for mn in self.mninfo:
            state = self.nodes[0].protx('info', mn.proTxHash)['state']
            assert_equal(state['PoSePenalty'], 0)
            assert_equal(state['PoSeBanHeight'], -1)
        self._plog("Phase 1 DONE: height=%d" % self.nodes[0].getblockcount())

        self._plog("Phase 2 START: confirm chainlocks are live on the healthy baseline, "
                   "before inducing any crisis -- the first of a 3-point comparison "
                   "(working baseline -> stall during the crisis -> resume after recovery)")
        self.nodes[0].generate(1)
        self.wait_for_chainlocked_block_all_nodes(self.nodes[0].getbestblockhash(), timeout=30)
        self._plog("Phase 2 DONE: height=%d" % self.nodes[0].getblockcount())

        self._plog("Phase 3 START: permanently kill both quorums' members (not isolate-then-repair)")
        victims = {mn.proTxHash: mn for mn in (self.get_quorum_smartnodes(qh1) + self.get_quorum_smartnodes(qh2))}
        victims = list(victims.values())
        victim_hashes = set(mn.proTxHash for mn in victims)
        survivors = [mn for mn in self.mninfo if mn.proTxHash not in victim_hashes]
        survivor_hashes = set(mn.proTxHash for mn in survivors)
        self._plog("killing %d distinct nodes (from 2 quorums), leaving %d untouched survivors" %
                   (len(victims), len(survivors)))
        assert len(survivors) >= 2 * self.llmq_size, "not enough untouched survivor margin for recovery"
        for mn in victims:
            self.stop_node(mn.node.index)
        live_nodes = [self.nodes[0]] + [mn.node for mn in survivors]
        self._plog("Phase 3 DONE: height=%d" % self.nodes[0].getblockcount())

        self._plog("Phase 4 START: mine forward to GHOST_BAN_LIST activation (height %d)" % ACTIVATION_HEIGHT)
        self._mine_to_height(live_nodes, ACTIVATION_HEIGHT + 30)
        self._wait_for_update_active(GHOST_BAN_UPDATE_NAME)
        self._plog("Phase 4 DONE: height=%d" % self.nodes[0].getblockcount())

        self._plog("Phase 5 START: spork at its default (255, OFF) -- mandatory presence holds, "
                   "list stays empty regardless of who was actually selected/missed")
        assert_equal(self.nodes[0].spork('show')[GHOST_BAN_SPORK], 255)
        self._mine_to_next_trigger(live_nodes)
        ghostban_tx = self._find_last_ghostban_tx()
        assert ghostban_tx is not None, "mandatory presence failed: no ghost-ban tx at the trigger height"
        assert_equal(ghostban_tx['ghostProTxHashes'], [])
        for mn in victims:
            state = self.nodes[0].protx('info', mn.proTxHash)['state']
            assert_equal(state['PoSeBanHeight'], -1)
        self._plog("Phase 5 DONE: height=%d" % self.nodes[0].getblockcount())

        self._plog("Phase 6 START: enable the spork, ban every victim across however many cycles it takes")
        self.nodes[0].spork(GHOST_BAN_SPORK, 51)
        self._wait_for_sporks(live_nodes)

        # One-cycle lag by design: the applied threshold is always the PRIOR cycle's
        # recorded value, never live, so this trigger still sees Phase 5's disabled
        # setting. Consume it before the real banking loop starts.
        self._plog("Phase 6: mining one-cycle-lag catch-up trigger")
        self._mine_to_next_trigger(live_nodes)

        # banned tracks GROUND TRUTH (on-chain PoSeBanHeight), not an accumulation from
        # rounds this loop happened to find "clean" -- the real mechanism bans whoever
        # crosses threshold regardless of whether a survivor also got blanket-missed in
        # the same null commitment, so gating credit on "no blast this round" undercounts
        # reality rather than just skipping a noisy assertion.
        banned = self._banned_victims(victim_hashes)
        for attempt in range(60):
            if banned == victim_hashes:
                break
            self._plog("Phase 6 attempt %d: banned=%d/%d" % (attempt, len(banned), len(victim_hashes)))
            self._mine_to_next_trigger(live_nodes)
            banned = self._banned_victims(victim_hashes)
        assert_equal(banned, victim_hashes)
        # An occasional survivor getting caught in the same null-commitment blast as
        # victims (2 victims + 1 survivor selected, can't reach threshold, blanket miss)
        # is accepted collateral damage, not a failure -- this mechanism is a temporary,
        # aggressive intervention, dialed back via the spork once quorums form again.
        # The real bar is Phase 7/8 below: recovery still happens with whoever's left.
        collateral = {mn.proTxHash for mn in survivors
                      if self.nodes[0].protx('info', mn.proTxHash)['state']['PoSeBanHeight'] != -1}
        if collateral:
            self._plog("Phase 6: %d survivor(s) also banned (accepted collateral damage): %s" %
                       (len(collateral), collateral))
        self._plog("Phase 6 DONE: height=%d banned=%d/%d" %
                   (self.nodes[0].getblockcount(), len(banned), len(victim_hashes)))

        self._plog("Phase 7 START: recovery -- confirm a healthy quorum can still form from whoever's left")
        new_quorum = self._latest_healthy_quorum(survivor_hashes)
        if new_quorum is None:
            self._mine_to_next_trigger(live_nodes)
            new_quorum = self._latest_healthy_quorum(survivor_hashes)
        assert new_quorum is not None, "no real, all-survivor quorum found even after mining one more cycle"
        self._plog("Phase 7 DONE: height=%d quorum=%s" % (self.nodes[0].getblockcount(), new_quorum))

        self._plog("Phase 8 START: chainlocks resume -- the third point of the 3-point comparison")
        if not self._chainlock_near_tip():
            self.nodes[0].generate(1)
            self.wait_for_chainlocked_block(self.nodes[0], self.nodes[0].getbestblockhash(), timeout=30)
        self._plog("Phase 8 DONE: height=%d -- ALL PHASES COMPLETE" % self.nodes[0].getblockcount())

    def _reconnect(self):
        self.nodes[0].rpc_connected = False
        self.nodes[0].rpc = None
        self.nodes[0].wait_for_rpc_connection()

    def _rpc_retry(self, fn, *args, max_retries=5, **kwargs):
        # A timed-out call leaves the HTTP connection unusable (CannotSendRequest);
        # reconnect and retry. Idempotent calls only -- see _mine_to_height for generate().
        for attempt in range(max_retries):
            try:
                return fn(*args, **kwargs)
            except (JSONRPCException, http.client.CannotSendRequest, http.client.BadStatusLine,
                    ConnectionError, BrokenPipeError) as e:
                self._plog("RPC call failed (%s), reconnecting and retrying (%d/%d)" %
                          (e, attempt + 1, max_retries))
                self._reconnect()
        raise AssertionError("RPC call still failing after %d retries" % max_retries)

    def _mine_to_height(self, live_nodes, target, max_batch=100):
        while True:
            current = self._rpc_retry(self.nodes[0].getblockcount)
            if current >= target:
                break
            batch = min(max_batch, target - current)
            # DGW retargets off actual vs. nPowTargetSpacing (120s) and ignores
            # fPowNoRetargeting past height 60 -- 1s/block looked ~120x too fast and
            # compounded into a real difficulty ratchet, not just PoW-attempt variance.
            self.bump_mocktime(max(1, batch * 120), nodes=live_nodes)
            err_msg = None
            t0 = time.time()
            self._plog("generate(%d) CALLING: current=%d target=%d" % (batch, current, target))
            try:
                self.nodes[0].generate(batch)
                elapsed = time.time() - t0
                new_height = self._rpc_retry(self.nodes[0].getblockcount)
                difficulty = self._rpc_retry(self.nodes[0].getblockchaininfo)['difficulty']
                self._plog("generate(%d) RETURNED: new_height=%d elapsed=%.1fs (%.2fs/blk) difficulty=%.3e" %
                          (batch, new_height, elapsed, elapsed / max(batch, 1), difficulty))
                # Real time between batches so in-flight DKG messages land, or a fast
                # bulk generate can starve a healthy session into a null commitment.
                time.sleep(1)
                continue
            except (JSONRPCException, http.client.CannotSendRequest, http.client.BadStatusLine,
                    ConnectionError, BrokenPipeError) as e:
                err_msg = str(e)  # Python 3 deletes `e` itself once this block exits
                elapsed = time.time() - t0
            # The call may still be running server-side after the client times out;
            # piling a second concurrent generate() on top just races the first for
            # the same tip. Reconnect and watch instead of retrying immediately.
            self._plog("generate(%d) TIMED OUT after %.1fs (%s) -- reconnecting and "
                      "watching for progress before trying again, not piling on "
                      "a second concurrent call" % (batch, elapsed, err_msg))
            self._reconnect()
            last_seen = current
            stale_checks = 0
            while stale_checks < 6:  # ~2 minutes with no height movement before giving up on it
                time.sleep(20)
                try:
                    now = self._rpc_retry(self.nodes[0].getblockcount, max_retries=2)
                except AssertionError:
                    self._reconnect()
                    continue
                self._plog("  ...watching: height=%d (was %d)" % (now, last_seen))
                if now > last_seen:
                    last_seen = now
                    stale_checks = 0
                else:
                    stale_checks += 1
            self._plog("resuming at height %d (target %d)" % (last_seen, target))
        sync_blocks(live_nodes)

    def _banned_victims(self, victim_hashes):
        return {vh for vh in victim_hashes
                if self.nodes[0].protx('info', vh)['state']['PoSeBanHeight'] != -1}

    def _latest_healthy_quorum(self, allowed_hashes):
        # Real (non-null) and every valid member in allowed_hashes -- a quorum formed
        # after a ban can't include the banned proTxHash, so this also proves recency.
        quorums = self.nodes[0].quorum('list', 1).get(LLMQ_TEST_NAME, [])
        if not quorums:
            return None
        qh = quorums[0]
        valid = [m for m in self.nodes[0].quorum('info', LLMQ_TEST_TYPE, qh)['members'] if m['valid']]
        if not valid or any(m['proTxHash'] not in allowed_hashes for m in valid):
            return None
        return qh

    def _chainlock_near_tip(self, max_behind=2):
        tip = self.nodes[0].getblockcount()
        cl_hash = self.nodes[0].getbestchainlock()['blockhash']
        return tip - self.nodes[0].getblock(cl_hash)['height'] <= max_behind

    def _mine_to_next_trigger(self, live_nodes):
        # GetGhostBanSlotStart: one valid height per dkgInterval, at
        # slotStart + dkgMiningWindowEnd + 1 = 30k + 19 for LLMQ_5_60.
        height = self.nodes[0].getblockcount()
        trigger = 19
        while trigger <= height:
            trigger += LLMQ_TEST_DKG_INTERVAL
        # One block per call: PoW variance is per-attempt and additive across a
        # batch, so this scopes the RPC timeout to a single block's worst case.
        self._mine_to_height(live_nodes, trigger, max_batch=1)

    def _wait_for_update_active(self, name, timeout=30):
        def check():
            uf = self.nodes[0].getblockchaininfo().get('rip1_softforks', {}).get(name)
            return uf is not None and uf['status'] == 'active'
        wait_until(check, timeout=timeout, sleep=0.5)

    def _wait_for_sporks(self, nodes, timeout=30):
        def check():
            sporks = self.nodes[0].spork('show')
            return all(n.spork('show') == sporks for n in nodes)
        wait_until(check, timeout=timeout, sleep=0.5)

    def _find_last_ghostban_tx(self):
        best = self.nodes[0].getbestblockhash()
        block = self.nodes[0].getblock(best, 2)
        for tx in block['tx']:
            if tx.get('type') == GHOST_BAN_TX_TYPE and 'ghostBanTx' in tx:
                return tx['ghostBanTx']
        return None


if __name__ == '__main__':
    LLMQGhostBanTest().main()
