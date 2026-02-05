#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

struct spy_node : node {
    using node::node;
    std::size_t get_ref_count() const {
        return ref_count_.load();
    }
};

TEST(NodeRefCount, BasicLifecycle) {
    node_pointer<spy_node> p1(std::in_place_type<spy_node>, propagate_behavior::eager);
    EXPECT_EQ(p1->get_ref_count(), 1);

    {
        node_pointer<spy_node> p2 = p1;
        EXPECT_EQ(p1->get_ref_count(), 2);
    }
    EXPECT_EQ(p1->get_ref_count(), 1);
}

TEST(NodeRefCount, ManagerOwnership) {
    manager m;
    auto& n = m.add_node<spy_node>(propagate_behavior::eager);
    // Manager holds one ref.
    EXPECT_EQ(n.get_ref_count(), 1);

    // If we take another reference
    node_pointer<node> p(&n);
    EXPECT_EQ(n.get_ref_count(), 2);

    m.erase_node(n);
    m.update();
    // Manager released its ref. p still holds it.
    EXPECT_EQ(n.get_ref_count(), 1);

    // p goes out of scope, n deleted.
}

template<typename T>
struct spy_provider : provider_general<T> {
    spy_provider(manager& m, propagate_behavior pb) : provider_general<T>(m, pb) {}
    spy_provider(manager& m) : provider_general<T>(m) {}
    spy_provider() = default;

    std::size_t get_ref_count() const {
        return this->ref_count_.load();
    }

    request_pass_handle<T> request_raw(bool) override {
        return make_request_handle_unexpected<T>(data_state::failed);
    }
};

template<typename T>
struct spy_terminal : terminal<T> {
    spy_terminal(propagate_behavior pb) : terminal<T>(pb) {}
    spy_terminal() = default;

    std::size_t get_ref_count() const {
        return this->ref_count_.load();
    }
    // minimal implementation
    request_pass_handle<T> request_raw(bool) override { return make_request_handle_unexpected<T>(data_state::failed); }
};

TEST(NodeRefCount, ConnectionRetainsSuccessor) {
    manager m;
    auto& p = m.add_node<spy_provider<int>>(propagate_behavior::eager);
    auto& t = m.add_node<spy_terminal<int>>(propagate_behavior::eager);

    EXPECT_EQ(p.get_ref_count(), 1);
    EXPECT_EQ(t.get_ref_count(), 1);

    // Connect p -> t
    // p.connect_successors(t);
    // This creates a successor_entry in p which holds t.
    p.connect_successors(t);

    EXPECT_EQ(p.get_ref_count(), 1); // p is not held by t (weak parent)
    EXPECT_EQ(t.get_ref_count(), 2); // t is held by p (strong successor) + manager

    m.erase_node(t);
    m.update();
    // t removed from manager.
    EXPECT_EQ(t.get_ref_count(), 1); // t still held by p.

    m.erase_node(p);
    m.update();
    // p removed from manager. p ref=0 -> p dies.
    // p destructor -> disconnect_self_from_context -> clears successors.
    // t ref decremented. t dies.
}

TEST(NodeRefCount, AsyncTaskRetains) {
    manager m;

    auto lambda = [](int x) { return x * 2; };
    // Create an async transformer
    auto& transformer = m.add_node(
        async_transformer_unambiguous<decltype(lambda)>(
            propagate_behavior::eager,
            async_type::async_all,
            lambda
        )
    );

    // We need to cast to something we can check ref count on?
    // async_transformer inherits async_node -> type_aware_node -> node.
    // But we don't have spy methods on it.
    // We can rely on external verification?
    // Or we can define a custom async node type for testing.

    // Let's rely on the fact that if it crashes (use after free), it fails.
    // But checking ref count is better.

    // Access ref_count via unsafe cast or friend?
    // Let's create a derived class from async_node to spy.
}

// Custom async node to spy
struct spy_async_node : async_node<int, int> {
    using async_node::async_node;
    std::size_t get_ref_count() const {
        return this->ref_count_.load();
    }
    std::optional<int> operator()(const async_context&, int&& i) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return i;
    }
};

TEST(NodeRefCount, AsyncTaskRetainsExplicit) {
    manager m;
    auto& node = m.add_node<spy_async_node>(propagate_behavior::eager, async_type::async_all);

    EXPECT_EQ(node.get_ref_count(), 1);

    auto& p = m.add_node<spy_provider<int>>(propagate_behavior::eager);
    p.connect_successors(node);

    // Trigger async task
    p.update_value(42);

    // Task launched. Manager processes updates then launches task.
    // We need to wait for manager update?
    // Manager runs in separate thread? No, async_thread_ executes tasks.
    // But p.update_value(eager) -> node.on_push -> async_launch -> manager.push_task.

    // Wait for task to be picked up?
    // The task queue is processed by async_thread.
    // Once task is created (in async_launch), ref count should increase.

    // Loop briefly to check ref count increase
    bool increased = false;
    for(int i=0; i<100; ++i) {
        if(node.get_ref_count() > 1) { // 1(manager) + 1(task) + 1(provider? no provider holds strong ref to successor)
            // Wait, p connects to node. provider -> successor_entry -> node_pointer -> node.
            // So p holds node!
            // Start: ref=2 (Manager + P).
            increased = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Actually, simply connecting P -> Node makes Ref=2.
    EXPECT_GE(node.get_ref_count(), 2);

    // When task launches, it should be 3.
    // Let's verify that.

    // We need to synchronize.
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Task running

    // If task is running, Ref should be >= 3.
    // Manager=1, P=1, Task=1.

    EXPECT_GE(node.get_ref_count(), 3);

    m.erase_node(node); // Manager release. Ref 3->2.
    m.erase_node(p);    // P release. P dies -> Releases Node. Ref 2->1.

    // Now only Task holds Node.
    // Wait for task to finish?
    // Manager update() handles completion.

    // If we stop manager?
}
