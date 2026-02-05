#include <gtest/gtest.h>
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

struct LifecycleTracker {
    std::shared_ptr<bool> destroyed;
    LifecycleTracker() : destroyed(std::make_shared<bool>(false)) {}
};

struct LifecycleNode : node {
    std::shared_ptr<bool> destroyed;

    LifecycleNode(std::shared_ptr<bool> d) : destroyed(d) {
        *destroyed = false;
    }

    ~LifecycleNode() override {
        if(destroyed) *destroyed = true;
    }
};

TEST(NodeLifetime, NodePointerBasic) {
    auto destroyed = std::make_shared<bool>(false);
    {
        node_pointer ptr{std::in_place_type<LifecycleNode>, destroyed};
        EXPECT_FALSE(*destroyed);

        node_pointer ptr2 = ptr; // Copy
        EXPECT_FALSE(*destroyed);

        ptr = nullptr; // ptr release
        EXPECT_FALSE(*destroyed);

        node_pointer ptr3 = std::move(ptr2); // Move
        EXPECT_FALSE(*destroyed);
        EXPECT_FALSE(ptr2);
        EXPECT_TRUE(ptr3);
    } // ptr3 goes out of scope
    EXPECT_TRUE(*destroyed);
}

struct LifecycleTerminal : terminal<int> {
    std::shared_ptr<bool> destroyed;

    LifecycleTerminal(std::shared_ptr<bool> d) : terminal<int>(propagate_behavior::eager), destroyed(d) {
        *destroyed = false;
    }

    ~LifecycleTerminal() override {
        if(destroyed) *destroyed = true;
    }

    request_pass_handle<int> request_raw(bool) override {
        return make_request_handle_unexpected<int>(data_state::failed);
    }
};

TEST(NodeLifetime, ConnectionKeepsAlive) {
    manager mgr;
    auto destroyed_downstream = std::make_shared<bool>(false);

    node_pointer upstream{std::in_place_type<provider_cached<int>>, mgr};

    {
        node_pointer downstream{std::in_place_type<LifecycleTerminal>, destroyed_downstream};

        auto* up_ptr = static_cast<provider_cached<int>*>(upstream.get());
        auto* down_ptr = static_cast<LifecycleTerminal*>(downstream.get());

        // Connect upstream -> downstream
        ASSERT_TRUE(up_ptr->connect_successors(*down_ptr));

        // downstream goes out of scope here.
        // It should be kept alive by upstream.
    }

    EXPECT_FALSE(*destroyed_downstream);

    // Destroy upstream
    upstream = nullptr;

    // Now downstream should be destroyed too
    EXPECT_TRUE(*destroyed_downstream);
}


TEST(NodeLifetime, AsyncTaskKeepsAlive) {
    manager mgr;
    auto destroyed = std::make_shared<bool>(false);

    std::binary_semaphore sem_start{0};
    std::binary_semaphore sem_finish{0};

    auto task_fn = [&, destroyed](const async_context&, int v) {
        sem_start.release();
        sem_finish.acquire(); // Wait until test allows us to finish
        return v;
    };

    node_pointer transformer{std::in_place_type<async_transformer<decltype(task_fn), int>>, propagate_behavior::eager, async_type::async_all, std::move(task_fn)};

    // We need to subclass async_transformer to hook destructor?
    // async_transformer is a final struct alias? No, it's a struct.
    // But we used a lambda.
    // Let's attach a listener to the transformer that we track instead.

    // Or just wrap the transformer in a struct that tracks destruction.
    struct TrackedTransformer : async_transformer<decltype(task_fn), int> {
         std::shared_ptr<bool> destroyed;
         TrackedTransformer(std::shared_ptr<bool> d, propagate_behavior pb, async_type at, decltype(task_fn) fn)
            : async_transformer(pb, at, std::move(fn)), destroyed(d) {
             *destroyed = false;
         }
         ~TrackedTransformer() override {
             if(destroyed) *destroyed = true;
         }
    };

    node_pointer tracked_node{std::in_place_type<TrackedTransformer>, destroyed, propagate_behavior::eager, async_type::async_all, task_fn};

    node_pointer provider{std::in_place_type<provider_cached<int>>, mgr};
    auto* prov_ptr = static_cast<provider_cached<int>*>(provider.get());
    auto* node_ptr = static_cast<TrackedTransformer*>(tracked_node.get());

    prov_ptr->connect_successors(*node_ptr);

    // Trigger task
    prov_ptr->update_value(42);

    // Wait for task to start
    sem_start.acquire();

    // Now task is running.
    // Drop our reference to the node.
    tracked_node = nullptr;
    prov_ptr->disconnect_successors(*node_ptr); // Disconnect from upstream so upstream doesn't hold it

    // Check it's still alive (held by task)
    EXPECT_FALSE(*destroyed);

    // Allow task to finish
    sem_finish.release();

    // Wait for task completion/cleanup.
    // We need to wait a bit or ensure the manager has processed it.
    // The task runs in a thread pool managed by manager?
    // Or std::async?
    // Memory says "manager.push_task".

    // We need to wait for the manager to finish processing.
    // This might require a small sleep or a loop checking the flag if there's no "wait for idle" in manager.
    int retries = 0;
    while(!*destroyed && retries < 100) {
        mgr.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        retries++;
    }

    EXPECT_TRUE(*destroyed);
}
