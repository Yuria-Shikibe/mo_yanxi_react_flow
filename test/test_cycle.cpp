#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import mo_yanxi.react_flow.common;
import std;

using namespace mo_yanxi::react_flow;

TEST(CycleTest, SimpleCycle) {
    manager mgr;
    // t1: int -> int (identity)
    auto& t1 = mgr.add_node(make_transformer(propagate_behavior::eager, [](int v){ return v; }));

    // t2: int -> int (identity)
    auto& t2 = mgr.add_node(make_transformer(propagate_behavior::eager, [](int v){ return v; }));

    // t1 -> t2
    t1.connect_successor(t2);

    // t2 -> t1 (should fail)
    try {
        t2.connect_successor(t1);
        FAIL() << "Expected invalid_node exception";
    } catch (const invalid_node_error& e) {
        EXPECT_STREQ(e.what(), "ring detected");
    } catch (...) {
        FAIL() << "Expected invalid_node exception";
    }
}

TEST(CycleTest, SelfCycle) {
    manager mgr;
    auto& t1 = mgr.add_node(make_transformer(propagate_behavior::eager, [](int v){ return v; }));

    // t1 -> t1 (should fail)
    try {
        t1.connect_successor(t1);
        FAIL() << "Expected invalid_node exception";
    } catch (const invalid_node_error& e) {
        EXPECT_STREQ(e.what(), "ring detected");
    } catch (...) {
        FAIL() << "Expected invalid_node exception";
    }
}

TEST(CycleTest, LargerCycle) {
    manager mgr;
    auto& t1 = mgr.add_node(make_transformer(propagate_behavior::eager, [](int v){ return v; }));
    auto& t2 = mgr.add_node(make_transformer(propagate_behavior::eager, [](int v){ return v; }));
    auto& t3 = mgr.add_node(make_transformer(propagate_behavior::eager, [](int v){ return v; }));

    t1.connect_successor(t2);
    t2.connect_successor(t3);

    // t3 -> t1 (should fail)
    try {
        t3.connect_successor(t1);
        FAIL() << "Expected invalid_node exception";
    } catch (const invalid_node_error& e) {
        EXPECT_STREQ(e.what(), "ring detected");
    } catch (...) {
        FAIL() << "Expected invalid_node exception";
    }
}
