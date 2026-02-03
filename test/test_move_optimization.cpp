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

    MoveTracker(std::string v = "") : value(std::move(v)) {}

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
        updated = true;
    }
};

TEST(MoveOptimizationTest, SingleConsumer_ViaTransformer) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<MoveTracker>>();

    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::none, [&](MoveTracker v){
        return v;
    }));

    bool updated = false;
    auto& t = mgr.add_node<TestListener>(propagate_behavior::eager, updated, "Single");

    p.connect_successors(trans);
    trans.connect_successors(t);

    MoveTracker source("payload");

    // Reset counters before update
    MoveTracker::reset();

    p.update_value(std::move(source));

    EXPECT_TRUE(updated);

    // Expected:
    // 1. update_value (move)
    // 2. provider -> trans (copy)
    // 3. trans return (move)
    // 4. trans -> storage (move) [Optimization!]
    // 5. storage -> listener (move)

    // Total Moves >= 4. Copies = 1.
    // If optimization failed (storage held pointer):
    // 4. trans -> storage (copy reference? or copy value?)
    // 5. storage -> listener (copy)

    // So checking high move count confirms optimization.
    EXPECT_GE(MoveTracker::move_count, 3) << "Expected at least 3 moves";
    EXPECT_EQ(MoveTracker::copy_count, 1) << "Expected exactly 1 copy (from provider cache)";
}

TEST(MoveOptimizationTest, MultipleConsumers_ViaTransformer) {
    manager mgr;
    auto& p = mgr.add_node<provider_cached<MoveTracker>>();

    auto& trans = mgr.add_node(make_transformer(propagate_behavior::eager, async_type::none, [&](MoveTracker v){
        return v;
    }));

    bool t1_updated = false;
    bool t2_updated = false;
    bool t3_updated = false;

    auto& t1 = mgr.add_node<TestListener>(propagate_behavior::eager, t1_updated, "T1");
    auto& t2 = mgr.add_node<TestListener>(propagate_behavior::eager, t2_updated, "T2");
    auto& t3 = mgr.add_node<TestListener>(propagate_behavior::eager, t3_updated, "T3");

    p.connect_successors(trans);

    // Order: T1, T2, T3
    trans.connect_successors(t1);
    trans.connect_successors(t2);
    trans.connect_successors(t3);

    MoveTracker source("payload");

    MoveTracker::reset();
    p.update_value(std::move(source));

    EXPECT_TRUE(t1_updated);
    EXPECT_TRUE(t2_updated);
    EXPECT_TRUE(t3_updated);

    // T1, T2 get copies. T3 gets move.
    EXPECT_GE(MoveTracker::move_count, 3);
}
