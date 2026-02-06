#include <gtest/gtest.h>
import std;
import mo_yanxi.react_flow;

using namespace mo_yanxi::react_flow;

// A simple mock node to track destruction
struct MockNode : public node {
    bool* destroyed_flag = nullptr;

    MockNode(bool* flag) : destroyed_flag(flag) {
        if(destroyed_flag) *destroyed_flag = false;
    }

    ~MockNode() override {
        if(destroyed_flag) *destroyed_flag = true;
    }
};

TEST(NodePointerTest, ConstructorRawPtr) {
    bool destroyed = false;
    {
        node_pointer ptr(new MockNode(&destroyed));
        EXPECT_TRUE(ptr);
        EXPECT_NE(ptr.get(), nullptr);
    } // ptr goes out of scope, ref count 1 -> 0, deletes node
    EXPECT_TRUE(destroyed);
}

TEST(NodePointerTest, ConstructorReference) {
    bool destroyed = false;
    MockNode* raw_node = new MockNode(&destroyed);
    {
        node_pointer ptr(*raw_node); // increments ref count (0->1)
        EXPECT_EQ(ptr.get(), raw_node);
    } // ptr goes out of scope, ref count 1 -> 0, deletes node
    EXPECT_TRUE(destroyed);
}

TEST(NodePointerTest, ConstructorInPlace) {
    bool destroyed = false;
    {
        node_pointer ptr(std::in_place_type<MockNode>, &destroyed);
        EXPECT_TRUE(ptr);
    }
    EXPECT_TRUE(destroyed);
}

TEST(NodePointerTest, CopyConstructor) {
    bool destroyed = false;
    {
        node_pointer ptr1(new MockNode(&destroyed));
        {
            node_pointer ptr2(ptr1); // Copy, ref count increments
            EXPECT_EQ(ptr1.get(), ptr2.get());
        } // ptr2 destroyed, ref count decr
        EXPECT_FALSE(destroyed);
    } // ptr1 destroyed, ref count decr -> 0
    EXPECT_TRUE(destroyed);
}

TEST(NodePointerTest, MoveConstructor) {
    bool destroyed = false;
    {
        node_pointer ptr1(new MockNode(&destroyed));
        node* raw = ptr1.get();
        {
            node_pointer ptr2(std::move(ptr1)); // Move, ownership transfer
            EXPECT_EQ(ptr2.get(), raw);
            EXPECT_FALSE(ptr1); // ptr1 is now null
        } // ptr2 destroyed, ref count decr -> 0
        EXPECT_TRUE(destroyed);
    }
    // ptr1 is null, destruction does nothing
}

TEST(NodePointerTest, CopyAssignment) {
    bool destroyed1 = false;
    bool destroyed2 = false;
    {
        node_pointer ptr1(new MockNode(&destroyed1));
        node_pointer ptr2(new MockNode(&destroyed2));

        ptr1 = ptr2; // ptr1 releases its node (destroyed1 becomes true), takes ptr2's node
        EXPECT_TRUE(destroyed1);
        EXPECT_FALSE(destroyed2);
        EXPECT_EQ(ptr1.get(), ptr2.get());
    } // both ptr1 and ptr2 destroyed, ref count of node 2 decr twice -> 0
    EXPECT_TRUE(destroyed2);
}

TEST(NodePointerTest, MoveAssignment) {
    bool destroyed1 = false;
    bool destroyed2 = false;
    {
        node_pointer ptr1(new MockNode(&destroyed1));
        node_pointer ptr2(new MockNode(&destroyed2));
        node* raw2 = ptr2.get();

        ptr1 = std::move(ptr2); // ptr1 releases node1, takes node2. ptr2 becomes null.
        EXPECT_TRUE(destroyed1);
        EXPECT_FALSE(destroyed2);
        EXPECT_EQ(ptr1.get(), raw2);
        EXPECT_FALSE(ptr2);
    }
    EXPECT_TRUE(destroyed2);
}

TEST(NodePointerTest, Reset) {
    bool destroyed = false;
    {
        node_pointer ptr(new MockNode(&destroyed));
        EXPECT_FALSE(destroyed);
        ptr.reset(); // ref count 0, delete
        EXPECT_TRUE(destroyed);
        EXPECT_FALSE(ptr);
    }
}

TEST(NodePointerTest, ResetWithNewNode) {
    bool destroyed1 = false;
    bool destroyed2 = false;
    {
        node_pointer ptr(new MockNode(&destroyed1));
        ptr.reset(new MockNode(&destroyed2));
        EXPECT_TRUE(destroyed1);
        EXPECT_FALSE(destroyed2);
    }
    EXPECT_TRUE(destroyed2);
}

TEST(NodePointerTest, DereferenceOperators) {
    bool destroyed = false;
    node_pointer ptr(new MockNode(&destroyed));
    EXPECT_EQ(&(*ptr), ptr.get());
    EXPECT_EQ(ptr.operator->(), ptr.get());
}

TEST(NodePointerTest, Equality) {
    bool d1 = false;
    bool d2 = false;
    node_pointer ptr1(new MockNode(&d1));
    node_pointer ptr2(ptr1);
    node_pointer ptr3(new MockNode(&d2));

    EXPECT_EQ(ptr1, ptr2);
    EXPECT_NE(ptr1, ptr3);
    EXPECT_EQ(ptr1, ptr1);
}
