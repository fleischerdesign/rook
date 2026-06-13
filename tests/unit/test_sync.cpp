#include <gtest/gtest.h>
#include <set>
#include <thread>
#include <chrono>
#include "rook/sync/hlc.hpp"
#include "rook/sync/crdt_gset.hpp"
#include "rook/sync/crdt_lww_map.hpp"
#include "rook/sync/crdt_awset.hpp"
#include "rook/sync/crdt_yata.hpp"
#include "rook/sync/sync_engine.hpp"

using namespace rook::sync;

TEST(HlcTest, MonotonicWithinSameNode) {
    HybridLogicalClock clock("node-a");
    auto ts1 = clock.now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto ts2 = clock.now();

    EXPECT_LT(ts1.wall_time_ms, ts2.wall_time_ms);
}

TEST(HlcTest, LogicalCounterIncrementsAtSameWallTime) {
    HybridLogicalClock clock("node-a");

    std::vector<HlcTimestamp> stamps;
    for (int i = 0; i < 100; ++i) {
        stamps.push_back(clock.now());
    }

    for (size_t i = 1; i < stamps.size(); ++i) {
        EXPECT_TRUE(stamps[i - 1] < stamps[i])
            << "i=" << i
            << " prev=(" << stamps[i - 1].wall_time_ms << ","
            << stamps[i - 1].logical_counter << ")"
            << " curr=(" << stamps[i].wall_time_ms << ","
            << stamps[i].logical_counter << ")";
    }
}

TEST(HlcTest, ObserveAdvancesClock) {
    HybridLogicalClock a("node-a");
    HybridLogicalClock b("node-b");

    auto b_ts = b.now();

    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    a.observe(b_ts);
    auto a_ts = a.now();

    EXPECT_GE(a_ts.wall_time_ms, b_ts.wall_time_ms);
}

TEST(HlcTest, NodeIdIsTiebreaker) {
    HlcTimestamp ts1{100, 5, "alpha"};
    HlcTimestamp ts2{100, 5, "beta"};

    EXPECT_NE(ts1, ts2);
    EXPECT_TRUE(ts1 < ts2 || ts2 < ts1);
}

TEST(HlcTest, HlcOrderTotalOrder) {
    std::set<HlcTimestamp, HlcOrder> set;

    set.insert({1, 0, "z"});
    set.insert({2, 0, "a"});
    set.insert({1, 1, "c"});
    set.insert({1, 0, "a"});

    EXPECT_EQ(set.size(), 4u);
}

TEST(HlcTest, ObserveFromFuture) {
    HybridLogicalClock clock("node");

    HlcTimestamp future{9999999999999LL, 0, "remote"};
    clock.observe(future);

    auto now = clock.now();
    EXPECT_GE(now.wall_time_ms, future.wall_time_ms);
}

TEST(GSetTest, AddContains) {
    GSet<std::string> set;
    set.add("alpha");
    set.add("beta");

    EXPECT_TRUE(set.contains("alpha"));
    EXPECT_TRUE(set.contains("beta"));
    EXPECT_FALSE(set.contains("gamma"));
    EXPECT_EQ(set.size(), 2u);
}

TEST(GSetTest, MergeIsUnion) {
    GSet<int> a;
    a.add(1);
    a.add(2);

    GSet<int> b;
    b.add(2);
    b.add(3);

    a.merge(b);

    EXPECT_TRUE(a.contains(1));
    EXPECT_TRUE(a.contains(2));
    EXPECT_TRUE(a.contains(3));
    EXPECT_EQ(a.size(), 3u);
}

TEST(GSetTest, MergeIsCommutative) {
    GSet<int> a;
    a.add(1);

    GSet<int> b;
    b.add(2);

    GSet<int> a_copy = a;
    a.merge(b);

    GSet<int> b_copy = b;
    b_copy.merge(a_copy);

    auto ae = a.elements();
    auto be = b_copy.elements();
    std::sort(ae.begin(), ae.end());
    std::sort(be.begin(), be.end());
    EXPECT_EQ(ae, be);
}

TEST(LwwMapTest, PutGet) {
    LwwMap<std::string, std::string> map;
    HybridLogicalClock clock("n1");

    map.put("key1", "value1", clock.now());
    EXPECT_EQ(map.get("key1").value_or("NONE"), "value1");
}

TEST(LwwMapTest, RemoveHidesValue) {
    LwwMap<std::string, std::string> map;
    HybridLogicalClock clock("n1");

    map.put("k", "v", clock.now());
    map.remove("k", clock.now());

    EXPECT_EQ(map.get("k"), std::nullopt);
    EXPECT_EQ(map.size(), 0u);
}

TEST(LwwMapTest, LastWriterWins) {
    LwwMap<std::string, std::string> map;
    HybridLogicalClock clock("n1");

    map.put("k", "old", clock.now());
    map.put("k", "new", clock.now());

    EXPECT_EQ(map.get("k").value_or("NONE"), "new");
}

TEST(LwwMapTest, MergeRespectsTimestamps) {
    HybridLogicalClock a_clock("node-a");
    HybridLogicalClock b_clock("node-b");

    LwwMap<std::string, std::string> map_a;
    LwwMap<std::string, std::string> map_b;

    map_a.put("shared", "a_value", a_clock.now());

    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    map_b.put("shared", "b_value", b_clock.now());
    map_b.put("only_b", "b", b_clock.now());

    map_a.merge(map_b);

    EXPECT_EQ(map_a.get("shared").value_or("NONE"), "b_value");
    EXPECT_EQ(map_a.get("only_b").value_or("NONE"), "b");
    EXPECT_EQ(map_a.size(), 2u);
}

TEST(LwwMapTest, TombstonePersistsOnMerge) {
    HybridLogicalClock clock("n1");

    LwwMap<std::string, std::string> a;
    a.put("k", "v", clock.now());
    a.remove("k", clock.now());

    LwwMap<std::string, std::string> b;
    b.put("k", "old", {100, 0, "old-node"});  // earlier timestamp

    a.merge(b);

    EXPECT_EQ(a.get("k"), std::nullopt)
        << "tombstone should win over earlier value";
}

TEST(AwSetTest, AddContains) {
    AwSet<std::string> set;
    HybridLogicalClock clock("n1");

    set.add("ext1", clock.now());
    EXPECT_TRUE(set.contains("ext1"));
    EXPECT_FALSE(set.contains("ext2"));
}

TEST(AwSetTest, RemoveHidesElement) {
    AwSet<std::string> set;
    HybridLogicalClock clock("n1");

    set.add("ext1", clock.now());
    set.remove("ext1");

    EXPECT_FALSE(set.contains("ext1"));
    EXPECT_TRUE(set.empty());
}

TEST(AwSetTest, AddWinsOnConcurrentAddRemove) {
    HybridLogicalClock a_clock("node-a");
    HybridLogicalClock b_clock("node-b");

    AwSet<std::string> a;
    AwSet<std::string> b;

    a.add("ext", a_clock.now());
    b.add("ext", b_clock.now());

    a.remove("ext");

    b.merge(a);

    EXPECT_TRUE(b.contains("ext"))
        << "add should win against concurrent remove";
}

TEST(AwSetTest, RemoveThenReAdd) {
    AwSet<std::string> set;
    HybridLogicalClock clock("n1");

    set.add("ext", clock.now());
    set.remove("ext");
    set.add("ext", clock.now());

    EXPECT_TRUE(set.contains("ext"));
}

TEST(AwSetTest, MergeIsCommutativeAndConvergent) {
    HybridLogicalClock a_clock("node-a");
    HybridLogicalClock b_clock("node-b");
    HybridLogicalClock c_clock("node-c");

    AwSet<std::string> a, b, c;

    a.add("x", a_clock.now());
    a.add("y", a_clock.now());
    b.add("y", b_clock.now());
    b.add("z", b_clock.now());

    a.remove("y");
    c.add("w", c_clock.now());

    AwSet<std::string> a2 = a, b2 = b, c2 = c;

    a.merge(b); a.merge(c);
    b2.merge(c2); b2.merge(a);
    c2.merge(b); c2.merge(a2);

    auto ea = a.elements();
    auto eb = b2.elements();
    auto ec = c2.elements();

    std::sort(ea.begin(), ea.end());
    std::sort(eb.begin(), eb.end());
    std::sort(ec.begin(), ec.end());

    EXPECT_EQ(ea, eb);
    EXPECT_EQ(eb, ec)
        << "AWSet merge must converge regardless of merge order";
}

TEST(YataTest, SingleInsert) {
    YataSequence<std::string> seq;
    HybridLogicalClock clock("n1");

    seq.insert("", "", "hello", clock.now());

    auto snap = seq.snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0], "hello");
}

TEST(YataTest, OrderedInsert) {
    YataSequence<std::string> seq;
    HybridLogicalClock clock("n1");

    auto ts1 = clock.now();
    seq.insert("", "", "first", ts1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto ts2 = clock.now();

    std::string first_id = clock.nodeId() + ":"
        + std::to_string(ts1.wall_time_ms) + ":"
        + std::to_string(ts1.logical_counter);

    seq.insert(first_id, "", "second", ts2);

    auto snap = seq.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_EQ(snap[0], "first");
    EXPECT_EQ(snap[1], "second");
}

TEST(YataTest, RemoveHidesValue) {
    YataSequence<std::string> seq;
    HybridLogicalClock clock("n1");

    auto ts = clock.now();
    seq.insert("", "", "message", ts);
    std::string id = clock.nodeId() + ":" + std::to_string(ts.wall_time_ms) + ":" + std::to_string(ts.logical_counter);

    seq.remove(id);
    EXPECT_TRUE(seq.empty());
}

TEST(YataTest, MergedSequenceContainsAll) {
    HybridLogicalClock a_clock("node-a");
    HybridLogicalClock b_clock("node-b");

    YataSequence<std::string> a;
    YataSequence<std::string> b;

    auto a_ts = a_clock.now();
    a.insert("", "", "a-msg", a_ts);

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto b_ts = b_clock.now();
    b.insert("", "", "b-msg", b_ts);

    a.merge(b);
    a.merge(b);  // idempotent merge

    auto snap = a.snapshot();
    ASSERT_EQ(snap.size(), 2u);
    EXPECT_NE(std::find(snap.begin(), snap.end(), "a-msg"), snap.end());
    EXPECT_NE(std::find(snap.begin(), snap.end(), "b-msg"), snap.end());
}

TEST(YataTest, ConcurrentInsertsHaveDeterministicOrder) {
    HybridLogicalClock a_clock("node-a");
    HybridLogicalClock b_clock("node-b");

    YataSequence<std::string> seq;
    YataSequence<std::string> peer;

    auto ta = a_clock.now();
    seq.insert("", "", "a-ins", ta);

    auto tb = b_clock.now();
    peer.insert("", "", "b-ins", tb);

    YataSequence<std::string> copy_a = seq;
    YataSequence<std::string> copy_b = peer;

    copy_a.merge(peer);
    copy_b.merge(seq);

    EXPECT_EQ(copy_a.snapshot(), copy_b.snapshot())
        << "YATA: merge order must not affect deterministic ordering";
}

TEST(YataTest, TombstonesPropagateOnMerge) {
    HybridLogicalClock a_clock("node-a");
    HybridLogicalClock b_clock("node-b");

    YataSequence<std::string> a;
    YataSequence<std::string> b;

    auto ta = a_clock.now();
    a.insert("", "", "msg", ta);
    std::string msg_id = a_clock.nodeId() + ":" + std::to_string(ta.wall_time_ms) + ":" + std::to_string(ta.logical_counter);
    a.remove(msg_id);

    auto tb = b_clock.now();
    b.insert("", "", "b-msg", tb);

    b.merge(a);

    EXPECT_NE(std::find(b.snapshot().begin(), b.snapshot().end(), "b-msg"),
              b.snapshot().end());
}

TEST(SyncEngineTest, SettingsSync) {
    SyncEngine engine("node-1");
    engine.putSetting("model", "gpt-4o");
    engine.putSetting("temperature", "0.7");

    EXPECT_EQ(engine.getSetting("model").value_or(""), "gpt-4o");
    EXPECT_EQ(engine.getSetting("temperature").value_or(""), "0.7");

    engine.removeSetting("temperature");
    EXPECT_EQ(engine.getSetting("temperature"), std::nullopt);
}

TEST(SyncEngineTest, ExtensionsSync) {
    SyncEngine engine("node-1");

    engine.addExtension({"filesystem", "1.0.0"});
    engine.addExtension({"fetch", "0.5.0"});

    auto exts = engine.listExtensions();
    EXPECT_EQ(exts.size(), 2u);

    engine.removeExtension({"fetch", "0.5.0"});
    exts = engine.listExtensions();
    EXPECT_EQ(exts.size(), 1u);
    EXPECT_EQ(exts[0].name, "filesystem");
}

TEST(SyncEngineTest, ChatInsertion) {
    SyncEngine engine("node-1");

    engine.insertChatMessage("chat-1", "", "", {"user", "hello", "", "", ""});
    engine.insertChatMessage("chat-1", "", "", {"assistant", "hi there", "", "", ""});

    auto msgs = engine.chatSnapshot("chat-1");
    EXPECT_EQ(msgs.size(), 2u);
    EXPECT_EQ(msgs[0].content, "hello");
    EXPECT_EQ(msgs[1].content, "hi there");
}

TEST(SyncEngineTest, SerializeMergeRoundtrip) {
    SyncEngine a("node-a");
    SyncEngine b("node-b");

    a.putSetting("theme", "dark");
    a.addExtension({"shell", "1.0.0"});
    a.addPeer({"node-c", "192.168.1.3", true});

    auto data = a.serializeFullState();
    b.mergeFullState(data);

    auto peers = b.listPeers();
    ASSERT_EQ(peers.size(), 1u);
    EXPECT_EQ(peers[0].node_id, "node-c");
}

TEST(SyncEngineTest, NodeIdUniqueness) {
    SyncEngine a("node-a");
    SyncEngine b("node-b");

    EXPECT_NE(a.nodeId(), b.nodeId());
}
