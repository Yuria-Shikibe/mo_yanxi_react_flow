#include <gtest/gtest.h>

import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

namespace {

struct MoveTracker {
    static int move_count;
    static int copy_count;
    std::string value;

    MoveTracker() : value("") {}
    MoveTracker(std::string v) : value(std::move(v)) {}

    MoveTracker(const MoveTracker& other) : value(other.value) {
        copy_count++;
    }

    MoveTracker(MoveTracker&& other) noexcept : value(std::move(other.value)) {
        move_count++;
    }

    MoveTracker& operator=(const MoveTracker& other) {
        if (this != &other) {
            value = other.value;
            copy_count++;
        }
        return *this;
    }

    MoveTracker& operator=(MoveTracker&& other) noexcept {
        if (this != &other) {
            value = std::move(other.value);
            move_count++;
        }
        return *this;
    }

    static void reset() {
        move_count = 0;
        copy_count = 0;
    }
};

int MoveTracker::move_count = 0;
int MoveTracker::copy_count = 0;

} // namespace

// Custom listener
struct TestListener : terminal<MoveTracker> {
    bool& updated;
    std::string name;

    TestListener(propagate_behavior pb, bool& u, std::string n)
        : terminal<MoveTracker>(pb), updated(u), name(std::move(n)) {}

    void on_update(const MoveTracker& data) override {
        // Ignore initial synchronization where value is empty
        if (data.value == "") return;

        updated = true;
        EXPECT_EQ(data.value, "payload") << "Listener " << name << " received wrong value";
    }
};

TEST(MoveOptimizationTest, SingleConsumer_ViaTransformer) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<MoveTracker>>();

    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, [&](MoveTracker v){
        return v;
    }));

    bool updated = false;
    auto& t = mgr.add_node<TestListener>(propagate_behavior::eager, updated, "Single");

    p.connect_successor(trans);
    trans.connect_successor(t);

    MoveTracker source("payload");
    ASSERT_EQ(source.value, "payload");

    // Reset counters before main update to ignore connection noise
    MoveTracker::reset();

    p.update_value(std::move(source));

    EXPECT_TRUE(updated);

    // Check counters BEFORE calling request() which adds copies
    // Expected Moves:
    // 1. p.update_value (move assign to cache)
    // 2. trans return (move)
    // 3. trans -> storage (move) [Optimization!]
    // 4. storage -> listener (move)
    // Total: 4 moves.
    EXPECT_GE(MoveTracker::move_count, 3) << "Expected at least 3 moves";

    // Expected Copies:
    // 1. provider -> trans arg (copy from cache)
    // Total: 1 copy.
    EXPECT_EQ(MoveTracker::copy_count, 1) << "Expected exactly 1 copy (from provider cache)";

    // Verify provider state (adds a copy)
    auto cached_p = p.request(false);
    ASSERT_TRUE(cached_p.has_value());
    EXPECT_EQ(cached_p->value, "payload");
}

TEST(MoveOptimizationTest, MultipleConsumers_ViaTransformer) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<MoveTracker>>();

    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, [&](MoveTracker v){
        return v;
    }));

    bool t1_updated = false;
    bool t2_updated = false;
    bool t3_updated = false;

    auto& t1 = mgr.add_node<TestListener>(propagate_behavior::eager, t1_updated, "T1");
    auto& t2 = mgr.add_node<TestListener>(propagate_behavior::eager, t2_updated, "T2");
    auto& t3 = mgr.add_node<TestListener>(propagate_behavior::eager, t3_updated, "T3");

    p.connect_successor(trans);

    trans.connect_successor(t1);
    trans.connect_successor(t2);
    trans.connect_successor(t3);

    MoveTracker source("payload");

    MoveTracker::reset();
    p.update_value(std::move(source));

    EXPECT_TRUE(t1_updated);
    EXPECT_TRUE(t2_updated);
    EXPECT_TRUE(t3_updated);

    // T1, T2 get copies. T3 gets move.
    // Provider -> Trans: 1 copy.
    // Trans return: 1 move.
    // Trans -> Storage (T1): pointer (no copy/move yet) -> T1 get (copy).
    // Trans -> Storage (T2): pointer (no copy/move yet) -> T2 get (copy).
    // Trans -> Storage (T3): move (1 move) -> T3 get (move).
    // Update value: 1 move.

    // Total Moves: 1(update) + 1(ret) + 1(storage) + 1(get) = 4.
    EXPECT_GE(MoveTracker::move_count, 3);
}
