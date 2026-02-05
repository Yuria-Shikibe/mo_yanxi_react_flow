#include <gtest/gtest.h>
#include <atomic>
#include <iostream>

import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

struct tracked_node : node {
    bool& destroyed_flag;

    tracked_node(bool& flag) : destroyed_flag(flag) {
        destroyed_flag = false;
    }

    ~tracked_node() override {
        destroyed_flag = true;
    }
};

struct tracked_provider : provider_cached<int> {
    bool& destroyed_flag;

    tracked_provider(manager& m, bool& flag) : provider_cached<int>(m), destroyed_flag(flag) {
        destroyed_flag = false;
    }

    ~tracked_provider() override {
        destroyed_flag = true;
    }
};

struct tracked_terminal : terminal_cached<int> {
    bool& destroyed_flag;

    tracked_terminal(bool& flag) : destroyed_flag(flag) {
        destroyed_flag = false;
    }

    ~tracked_terminal() override {
        destroyed_flag = true;
    }
};

TEST(RefCounting, NodePointerLifecycle) {
    bool destroyed = false;
    {
        node_pointer ptr = node_pointer(std::in_place_type<tracked_node>, destroyed);
        ASSERT_FALSE(destroyed);
        ASSERT_EQ(ptr->get_ref_count(), 1);
    }
    ASSERT_TRUE(destroyed);
}

TEST(RefCounting, ConnectionLifecycle) {
    manager mgr;
    bool src_destroyed = false;
    bool sink_destroyed = false;

    {
        auto src = mgr.add_node<tracked_provider>(src_destroyed);
        auto sink = mgr.add_node<tracked_terminal>(sink_destroyed);

        ASSERT_EQ(src->get_ref_count(), 1);
        ASSERT_EQ(sink->get_ref_count(), 1);

        src->connect_successors(*sink);

        // sink should have 2 refs (local ptr + src connection)
        ASSERT_EQ(sink->get_ref_count(), 2);

        // Drop sink local pointer
        sink = nullptr;
        ASSERT_FALSE(sink_destroyed);

        // Drop src local pointer
        src = nullptr;
        // src should be destroyed immediately as it has 0 refs (no parents, local dropped)
        ASSERT_TRUE(src_destroyed);

        // sink should also be destroyed because src destructor releases it
        ASSERT_TRUE(sink_destroyed);
    }
}

TEST(RefCounting, ConnectionDisconnection) {
    manager mgr;
    bool src_destroyed = false;
    bool sink_destroyed = false;

    auto src = mgr.add_node<tracked_provider>(src_destroyed);
    auto sink = mgr.add_node<tracked_terminal>(sink_destroyed);

    src->connect_successors(*sink);
    ASSERT_EQ(sink->get_ref_count(), 2);

    src->disconnect_successors(*sink);
    ASSERT_EQ(sink->get_ref_count(), 1);

    sink = nullptr;
    ASSERT_TRUE(sink_destroyed);

    src = nullptr;
    ASSERT_TRUE(src_destroyed);
}

struct async_test_node : async_node<int, int> {
    bool& destroyed_flag;
    std::promise<void>& finish_promise;

    async_test_node(bool& flag, std::promise<void>& p)
        : async_node<int, int>(async_type::async_latest), destroyed_flag(flag), finish_promise(p) {
        destroyed_flag = false;
    }

    ~async_test_node() override {
        destroyed_flag = true;
    }

    std::optional<int> operator()(const async_context& ctx, int&& arg) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        finish_promise.set_value();
        return arg;
    }
};

TEST(RefCounting, AsyncLifecycle) {
    manager mgr;
    bool destroyed = false;
    std::promise<void> p;
    auto f = p.get_future();

    {
        auto node = mgr.add_node<async_test_node>(destroyed, p);

        // Setup simple flow to trigger async
        auto src = mgr.add_node<provider_cached<int>>();
        src->connect_successors(*node);

        src->update_value(42);
        mgr.update(); // Trigger async launch

        // At this point, async task should be running and holding a ref
        // We drop our local ref
        node = nullptr;
        ASSERT_FALSE(destroyed);

        // Wait for finish
        f.wait();

        // Process completion
        mgr.update();
    }
    // Now task is done and processed, node should be destroyed
    // Note: manager might hold task in 'done_' list until next update loop or destruction?
    // manager.update() processes done tasks and releases them.
    // The task destructor releases the node.
    ASSERT_TRUE(destroyed);
}

TEST(RefCounting, PulseNodeLifecycle) {
    manager mgr;
    bool destroyed = false;

    {
        // Create a node with pulse behavior
        // tracked_node defaults to propagate_behavior::eager (default ctor of node).
        // tracked_node ctor calls base node ctor.
        struct pulse_node : tracked_node {
            pulse_node(bool& f) : tracked_node(f) {
                this->data_propagate_type_ = propagate_behavior::pulse;
            }
        };

        auto ptr = mgr.add_node<pulse_node>(destroyed);

        // Pulse nodes are registered with manager, so manager holds a ref.
        // Local ref + Manager ref = 2
        ASSERT_EQ(ptr->get_ref_count(), 2);

        ptr = nullptr;
        // Still alive because manager holds it
        ASSERT_FALSE(destroyed);

        // How to kill it? erase_node
        // But we don't have ptr anymore.
        // Realistically user keeps ptr if they want to erase it.
    }
    // Manager destruction should release pulse nodes
    ASSERT_FALSE(destroyed); // Manager still alive
    {
        manager mgr2;
        auto ptr = mgr2.add_node<tracked_node>(destroyed);
        // Force propagate type change to register?
        // process_node is called during add_node.
        // tracked_node default is EAGER. Manager doesn't hold ref.
        ASSERT_EQ(ptr->get_ref_count(), 1);
    }
}
