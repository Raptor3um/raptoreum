#!/usr/bin/env python3
# Copyright (c) 2026 The Raptoreum developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

from test_framework.test_framework import RaptoreumTestFramework, LLMQ_TEST_DKG_INTERVAL, LLMQ_TEST_NAME
from test_framework.util import assert_equal, sync_blocks, wait_until

'''
feature_llmq_absence_tracking.py

Regression test for the cross-round DKG absence tracker (follow-up to
#500). Two stages, both against genuinely-offline members
(setnetworkactive(False), not quorum('dkgsimerror', ...) -- a node that
only omits one DKG message type while still relaying its own
absence-vote normally can never be marked confirmedAbsent, since
CheckAbsences() skips a member's own vote about itself, and that
non-self-accusing vote reaches every bundle):

1. One member offline, quorum still forms fine (4 of 5, well above
   threshold=3). Checks the basic mechanics: roundsSelected/
   roundsReportedAbsent accumulate correctly and consistently across two
   independent observers. Only one round: a commitment landing lets the
   *existing*, pre-#500 PoSePunish path penalize the absent member for
   real (confirmed live -- penalty went 0->66->100, banned, within this
   one round, since both llmq_test and llmq_test_v17 penalize it
   simultaneously), which would shrink the candidate pool from here on.
   That's correct, expected behaviour for a round that succeeds, not
   something this test fights -- it just doesn't rely on that member
   staying a candidate afterward.

2. Two more members taken offline on top of that (3 of 5 non-responsive)
   -- this is the actual mainnet failure mode #499 is about. Phantom
   smartnodes stay in the enabled/valid list forever on mainnet because
   PoSePunish's one call site is only reached "when a block connects a
   non-null final commitment" (see the #499 GitHub thread) -- a quorum
   that's phantom-heavy enough to fail outright never lands one, so it
   never fires there either. DKG sessions still run on schedule (phase
   transitions are driven by block height alone -- HandlePhase()/
   WaitForNextPhase() don't gate on participant count), but BLS
   threshold reconstruction needs >= threshold real contributions and
   there aren't enough, so no commitment is ever produced. The point of
   this stage: HandleDKGRound() runs CheckAbsences() "regardless of
   whether finalCommitments is empty" (see its own comment in
   quorums_dkgsessionhandler.cpp) -- so absence evidence, and this
   tracker's accumulation of it, must keep working even while the
   network is in exactly the stuck state that motivated building it.
   No healing needed in this stage: with no commitment ever landing,
   PoSePunish never fires, so there is no ban risk to manage.

The candidate-list-shrinks-when-a-member-is-PoSe-banned edge case (the
real params.size vs actual-candidate-count mismatch found while
live-testing this feature) is covered separately by
src/test/quorums_dkgabsencetracker_tests.cpp's unit tests -- stage 1
above demonstrates it happens for real, but doesn't assert on the exact
shape of it.

NOTE: stage 2 has also exposed what looks like a genuine, intermittent
daemon-side concurrency issue in CDKGSessionHandler, independent of
anything in this test -- see docs/findings/dkg-phase-wait-stall.md. This
test is registered in EXTENDED_SCRIPTS, not BASE_SCRIPTS, until that is
resolved or shown not to recur once run reliably many times in a row.
'''


class LLMQAbsenceTracking(RaptoreumTestFramework):
    def set_test_params(self):
        # fast_dip3_enforcement is left off: it appends -dip3params, which
        # this binary's arg parser rejects outright (confirmed by running
        # it) -- DIP3/DIP8 have no activation-height gate left in this
        # codebase at all (getblockchaininfo's softforks is always empty;
        # quorum/protx RPCs work from genesis), so there is nothing to wait
        # for in the first place.
        self.set_raptoreum_test_params(6, 5, [["-whitelist=127.0.0.1"]] * 6)
        self.set_raptoreum_llmq_test_params(5, 3)

    def isolate(self, mn):
        self.log.info("Taking %s... offline for real (setnetworkactive, not dkgsimerror)" % mn.proTxHash[:10])
        mn.node.setnetworkactive(False)
        wait_until(lambda: mn.node.getconnectioncount() == 0)

    def mine_degraded_round(self, nodes_to_watch):
        # Mirrors mine_quorum()'s own mechanics (test_framework.py) for the
        # phases that matter here: align to the real DKG interval with one
        # burst using the framework's own LLMQ_TEST_DKG_INTERVAL constant,
        # then *poll* via wait_for_quorum_phase() -- no re-mining inside the
        # poll, matching the stock helper exactly rather than reinventing
        # it. (An earlier version of this method hand-rolled a one-block-
        # at-a-time search against a guessed, wrong interval; that was this
        # test's own bug, not a real framework limitation -- mine_quorum()
        # already gets this right.) Never waits for (or mines) a final
        # commitment: with real contributors below threshold, BLS
        # threshold reconstruction cannot produce one, so there is nothing
        # to wait for there. Polls only nodes_to_watch's own phase (not an
        # exact total-member count): once a member is offline long enough
        # to be PoSe-banned, it drops out of the candidate pool entirely,
        # so how many total members a round actually has isn't something
        # this test wants to predict.
        nodes = [self.nodes[0]] + [mn.node for mn in nodes_to_watch]

        height = self.nodes[0].getblockcount()
        skip_count = LLMQ_TEST_DKG_INTERVAL - (height % LLMQ_TEST_DKG_INTERVAL)
        if skip_count != 0:
            self.bump_mocktime(1, nodes=nodes)
            self.nodes[0].generate(skip_count)
        sync_blocks(nodes)

        q = self.nodes[0].getbestblockhash()
        self.wait_for_quorum_phase(q, 1, len(nodes_to_watch), None, 0, nodes_to_watch)
        for phase in range(2, 7):
            self.bump_mocktime(1, nodes=nodes)
            self.nodes[0].generate(2)
            sync_blocks(nodes)
            self.wait_for_quorum_phase(q, phase, len(nodes_to_watch), None, 0, nodes_to_watch)
        return q

    def snapshot(self, label, quorum_hash):
        # The per-round absence record (and this tracker's update from it)
        # is built during a fixed 6s *real* wall-clock drain after Finalize
        # (quorums_dkgsessionhandler.cpp's GetTimeMillis()-based wait) --
        # deliberately not tied to mocktime -- with BuildAbsenceRecord()
        # running right after. Poll for *this* round's own record to
        # actually land (matched by quorum_hash) instead of guessing a
        # fixed sleep long enough to outlast it.
        for observer in (self.mninfo[3], self.mninfo[4]):
            wait_until(lambda o=observer: o.node.quorum("dkgstatus").get("absenceRecords", {})
                       .get(LLMQ_TEST_NAME, {}).get("quorumHash") == quorum_hash,
                       timeout=20, sleep=0.5)

        self.log.info("=== %s ===" % label)
        views = []
        for observer in (self.mninfo[3], self.mninfo[4]):
            status = observer.node.quorum("dkgstatus", 2)
            stats = status.get("absenceStats", {}).get(LLMQ_TEST_NAME, [])
            by_pro_tx = {s["proTxHash"]: s for s in stats}
            views.append(by_pro_tx)
            self.log.info("  observer %s...: %r" % (observer.proTxHash[:10], by_pro_tx))
        return views

    def run_test(self):
        self.nodes[0].spork("SPORK_17_QUORUM_DKG_ENABLED", 0)
        self.wait_for_sporks_same()

        offline_mn, more_offline_a, more_offline_b = self.mninfo[0], self.mninfo[1], self.mninfo[2]
        always_online = [self.mninfo[3], self.mninfo[4]]

        self.log.info("Round 1: clean quorum, everyone online")
        q1 = self.mine_quorum()
        views = self.snapshot("after round 1", q1)
        # The isolated member doesn't exist yet -- every member is genuinely
        # online, so both observers' values are deterministic. Assert on
        # both, not just one.
        for view in views:
            for mn in self.mninfo:
                assert_equal(view[mn.proTxHash]["roundsSelected"], 1)
                assert_equal(view[mn.proTxHash]["roundsReportedAbsent"], 0)

        self.log.info("Stage 1: taking %s... offline, quorum should still form (4 of 5, above threshold=3)" %
                      offline_mn.proTxHash[:10])
        self.isolate(offline_mn)
        mninfos_online = [mn for mn in self.mninfo if mn is not offline_mn]
        q2 = self.mine_quorum(expected_connections=1, expected_members=4, expected_contributions=4,
                              expected_complaints=4, expected_commitments=4, mninfos_online=mninfos_online)
        views = self.snapshot("after round 2 (stage 1)", q2)
        # offline_mn is isolated (setnetworkactive(False), zero connections
        # confirmed) before this round starts, so every vote in every
        # bundle marks it absent -- deterministic for both observers, not
        # just "likely". Assert on both.
        for view in views:
            for mn in self.mninfo:
                assert_equal(view[mn.proTxHash]["roundsSelected"], 2)
                assert_equal(view[mn.proTxHash]["roundsReportedAbsent"], 1 if mn is offline_mn else 0)
        self.log.info("Stage 1 done: quorum formed with 1 of 5 offline, correctly recorded by both observers. "
                      "(The existing PoSePunish path may now ban %s... for real, since a commitment landed -- "
                      "expected, not something this test relies on avoiding.)" % offline_mn.proTxHash[:10])

        self.log.info("Stage 2: taking 2 MORE offline (%s..., %s...) -- below threshold=3, finalization must fail" %
                      (more_offline_a.proTxHash[:10], more_offline_b.proTxHash[:10]))
        pre_degradation_top = self.nodes[0].quorum("list", 1)[LLMQ_TEST_NAME][0]
        self.isolate(more_offline_a)
        self.isolate(more_offline_b)

        def snapshot_baselines(views):
            # roundsSelected/roundsReportedAbsent are totals since round 1,
            # where these two were genuinely online and correctly never
            # reported absent -- what must hold from a given point on is the
            # delta since that point, not the absolute totals. Captured
            # per-observer, since each observer's own view is what its own
            # later delta gets checked against.
            return [{mn.proTxHash: (v[mn.proTxHash]["roundsSelected"], v[mn.proTxHash]["roundsReportedAbsent"])
                     for mn in (more_offline_a, more_offline_b)} for v in views]

        baselines = snapshot_baselines(views)
        degradation_passes = 2
        for pass_num in range(1, degradation_passes + 1):
            self.log.info("Degraded mining pass %d/%d: 3 of 5 non-responsive, expecting NO commitment" %
                          (pass_num, degradation_passes))
            # mine_degraded_round() drives however many DKG rounds its own
            # internal polling happens to cross in one pass -- so this
            # asserts the mechanism's actual invariant (always absent when
            # selected, since the baseline) rather than a specific count
            # that depends on exactly how many rounds a given pass produced.
            q = self.mine_degraded_round(always_online)

            top_now = self.nodes[0].quorum("list", 1)[LLMQ_TEST_NAME][0]
            assert_equal(top_now, pre_degradation_top)

            views = self.snapshot("after degraded pass %d (no commitment landed)" % pass_num, q)
            # Checked independently against each observer's own prior view
            # of itself -- the invariant holds per-observer regardless of
            # which specific rounds each one completed, so there's no need
            # to fall back to checking only one.
            for view, baseline in zip(views, baselines):
                for mn in (more_offline_a, more_offline_b):
                    selected_before, absent_before = baseline[mn.proTxHash]
                    selected_delta = view[mn.proTxHash]["roundsSelected"] - selected_before
                    absent_delta = view[mn.proTxHash]["roundsReportedAbsent"] - absent_before
                    assert selected_delta > 0, \
                        "%s... was not selected into any degraded round in pass %d" % (mn.proTxHash[:10], pass_num)
                    assert_equal(absent_delta, selected_delta)
            # Re-baseline so the next pass has to prove something new,
            # rather than a looser check that's already implied by this
            # pass having passed.
            baselines = snapshot_baselines(views)

        self.log.info("Final tally (already checked above; this is just the summary):")
        for mn in self.mninfo:
            s = views[0].get(mn.proTxHash)
            if s is None:
                self.log.info("  %s... -- not a candidate this round (likely PoSe-banned after round 2)" %
                              mn.proTxHash[:10])
            else:
                self.log.info("  %s... -- selected %d, reported absent %d" %
                              (mn.proTxHash[:10], s["roundsSelected"], s["roundsReportedAbsent"]))


if __name__ == '__main__':
    LLMQAbsenceTracking().main()
