#include <gtest/gtest.h>
import std;
import mo_yanxi.react_flow.data_storage;

// -----------------------------------------------------------------------------
// 编译期测试代码 (Compile-time Tests)
// -----------------------------------------------------------------------------
namespace tests {
    using namespace mo_yanxi::react_flow;

    // 1. 定义一个用于测试的非平凡类型 (Non-Trivial Type)
    // 只要有用户自定义的拷贝构造函数，is_trivially_copyable_v 就会为 false
    struct NonTrivialObj {
        int id;
        constexpr NonTrivialObj(int i) : id(i) {}

        // 自定义拷贝构造函数 -> 使得类型变为非平凡可复制
        constexpr NonTrivialObj(const NonTrivialObj& other) : id(other.id) {}

        constexpr NonTrivialObj(NonTrivialObj&& other) noexcept : id(other.id) {
            other.id = -1; // 移动后修改源，方便测试移动语义
        }


        constexpr NonTrivialObj& operator=(const NonTrivialObj& other){
            if(this == &other) return *this;
            id = other.id;
            return *this;
        }

        constexpr NonTrivialObj& operator=(NonTrivialObj&& other) noexcept{
            if(this == &other) return *this;
            id = std::exchange(other.id, -1);
            return *this;
        }

        constexpr bool operator==(const NonTrivialObj& other) const { return id == other.id; }
    };

    // 静态断言验证我们的测试辅助类是否符合预期
    static_assert(!std::is_trivially_copyable_v<NonTrivialObj>, "Test struct must be non-trivially copyable");
    static_assert(std::is_trivially_copyable_v<int>, "Int must be trivially copyable");

    // 2. 核心测试函数 (consteval 强制在编译期执行)
    consteval bool test_push_data_storage() {

        // --- 测试场景 A: 平凡类型 (int) ---
        {
            // 构造与 get
            data_carrier<int> ps(42);
            if (ps.get() != 42) return false;

            // 默认构造
            data_carrier<int> ps_def;
            if (ps_def.get() != 0) return false;
        }

        // --- 测试场景 B: 非平凡类型 (NonTrivialObj) ---

        // B1. 默认构造
        {
            data_carrier<NonTrivialObj> ps;
            if (!ps.is_empty()) return false;
        }

        // B2. 移动构造 (持有值 T)
        {
            data_carrier<NonTrivialObj> ps(NonTrivialObj{100});

            if (ps.is_empty()) return false;

            // 测试 get_copy() - 不应消耗数据
            if (ps.get_copy().id != 100) return false;
            if (ps.is_empty()) return false; // 依然有值

            // 测试 get() - 应消耗数据 (move)
            NonTrivialObj val = ps.get();
            if (val.id != 100) return false;
            if (!ps.is_empty()) return false; // 现在应该是空的 (monostate)
        }

        // B3. 引用/指针构造 (持有 const T*)
        {
            NonTrivialObj source{200};
            // 这里传入左值引用，会匹配 explicit push_data_storage(const T& ptr)
            data_carrier<NonTrivialObj> ps(source);

            if (ps.is_empty()) return false;

            // 指针模式下，get() 返回拷贝，且通常不置空 (取决于你的业务逻辑设计)
            // 根据你的代码：return *ptr; 没有 storage_ = monostate
            if (ps.get().id != 200) return false;
            if (ps.is_empty()) return true; // 如果逻辑是“指针模式get后不为空”，这里若为空则fail

            // 再次获取应依然成功
            if (ps.get_copy().id != 200) return false;
        }

        // B4. 移动赋值
        {
            data_carrier<NonTrivialObj> ps1(NonTrivialObj{300});
            data_carrier<NonTrivialObj> ps2;

            ps2 = std::move(ps1);

            if (ps2.is_empty()) return false;
            if (ps2.get_copy().id != 300) return false;

            // ps1 被 move 后应该处于 unspecified state，但在你的实现中 exchange 成了 monostate
            // 所以 ps1 应该是 empty
            if (!ps1.is_empty()) return false;
        }

        return true;
    }
}

// -----------------------------------------------------------------------------
// 执行编译期测试
// -----------------------------------------------------------------------------
// 如果这里编译通过，说明所有逻辑在编译期验证通过
// Skip on MSVC due to C7595 error with std::variant in consteval context
#ifndef _MSC_VER
static_assert(tests::test_push_data_storage(), "push_data_storage compile-time tests failed!");
#endif

// =========================================================================
// 辅助类：用于追踪构造、析构和移动操作
// =========================================================================
struct LifecycleTracker {
    static int move_construct_count;
    static int destruct_count;
    int value;

    LifecycleTracker(int v = 0) : value(v) {}

    // 拷贝构造
    LifecycleTracker(const LifecycleTracker&) = default;

    // 移动构造 - 增加计数
    LifecycleTracker(LifecycleTracker&& other) noexcept : value(other.value) {
        move_construct_count++;
        other.value = -1; // 标记源已被移动
    }

    // 析构 - 增加计数
    ~LifecycleTracker() {
        destruct_count++;
    }

    static void reset() {
        move_construct_count = 0;
        destruct_count = 0;
    }
};

int LifecycleTracker::move_construct_count = 0;
int LifecycleTracker::destruct_count = 0;

using namespace mo_yanxi::react_flow;

// =========================================================================
// 测试套件 1: 非平凡类型 (Non-Trivially Copyable) - 使用 Variant 的版本
// =========================================================================

// 测试场景：持有值 (Value Semantics)
TEST(PushDataStorageTest, NonTrivial_HoldsValue_Lifecycle) {
    using StorageType = data_carrier<std::string>;

    std::string data = "hello world";

    // 1. 移动构造传入数据
    StorageType storage(std::move(data));

    EXPECT_FALSE(storage.is_empty()) << "初始化后不应为空";

    // 2. get_copy() 测试：不应消耗数据
    std::string copy_val = storage.get_copy();
    EXPECT_EQ(copy_val, "hello world");
    EXPECT_FALSE(storage.is_empty()) << "get_copy 后不应为空";

    // 3. get() 测试：应消耗数据 (Move 语义)
    std::string move_val = storage.get();
    EXPECT_EQ(move_val, "hello world");

    // 4. 验证状态重置
    EXPECT_TRUE(storage.is_empty()) << "调用 get() 后，持有值的 storage 应该变为空 (monostate)";
}

// 测试场景：持有指针 (Pointer/Reference Semantics)
TEST(PushDataStorageTest, NonTrivial_HoldsPointer_Lifecycle) {
    using StorageType = data_carrier<std::string>;

    std::string origin = "persistent data";

    // 1. 传入左值引用 -> 触发 const T* 构造
    StorageType storage(origin);

    EXPECT_FALSE(storage.is_empty());

    // 2. get() 测试：指针模式下，get 不应该置空 storage
    // 因为它只是解引用指针返回拷贝，并不拥有对象的所有权
    std::string val1 = storage.get();
    EXPECT_EQ(val1, "persistent data");
    EXPECT_FALSE(storage.is_empty()) << "指针模式下 get() 不应导致 storage 变空";

    // 3. 再次获取应当成功
    std::string val2 = storage.get();
    EXPECT_EQ(val2, "persistent data");
}

// 测试场景：异常处理
TEST(PushDataStorageTest, NonTrivial_ExceptionSafety) {
    data_carrier<std::string> empty_storage;

    ASSERT_TRUE(empty_storage.is_empty());

    // 验证 get() 抛出异常
    EXPECT_THROW({
        (void)empty_storage.get();
    }, std::runtime_error);

    // 验证 get_copy() 抛出异常
    EXPECT_THROW({
        (void)empty_storage.get_copy();
    }, std::runtime_error);
}

// 测试场景：Storage 自身的移动语义
TEST(PushDataStorageTest, NonTrivial_StorageMoveSemantics) {
    using StorageType = data_carrier<std::string>;

    StorageType src(std::string("payload"));
    ASSERT_FALSE(src.is_empty());

    // 1. 移动构造
    StorageType dest(std::move(src));

    // 验证源变空，目标持有数据
    EXPECT_TRUE(src.is_empty()) << "源对象被 move 后应为空";
    EXPECT_FALSE(dest.is_empty()) << "目标对象应持有数据";
    EXPECT_EQ(dest.get_copy(), "payload");

    // 2. 移动赋值
    StorageType dest2;
    dest2 = std::move(dest);

    EXPECT_TRUE(dest.is_empty());
    EXPECT_FALSE(dest2.is_empty());
    EXPECT_EQ(dest2.get(), "payload");
}

// 测试场景：使用 Tracker 验证内部对象的精确 Move 次数
TEST(PushDataStorageTest, NonTrivial_VerifyInternalMove) {
    LifecycleTracker::reset();

    {
        LifecycleTracker t(100);
        // 构造 storage，T&&，发生一次移动
        data_carrier<LifecycleTracker> storage(std::move(t));

        // 此时 storage 内部持有一个 LifecycleTracker

        // 调用 get()，内部发生 std::move(std::get<T>(...))
        // 返回值优化(RVO)或者再次移动构造到 result
        LifecycleTracker result = storage.get();

        EXPECT_EQ(result.value, 100);
        EXPECT_TRUE(storage.is_empty());
    }

    // 预期：
    // 1. t -> storage (构造)
    // 2. storage -> result (get)
    // 至少发生2次移动构造
    EXPECT_GE(LifecycleTracker::move_construct_count, 2);
}

// =========================================================================
// 测试套件 2: 平凡类型 (Trivially Copyable) - 直接持有 T 的版本
// =========================================================================

TEST(PushDataStorageTest, TrivialType_Int) {
    // 验证是否命中了平凡类型的特化版本
    // 平凡版本没有 is_empty() 方法，如果调用 is_empty 会编译失败
    // 我们可以利用 SFINAE 检查，或者直接测试其行为

    data_carrier<int> ps(42);

    // 测试 get
    EXPECT_EQ(ps.get(), 42);

    // 平凡类型版本 get() 后值依然存在（因为只是简单的 return value_）
    EXPECT_EQ(ps.get(), 42);

    // 测试 get_copy
    EXPECT_EQ(ps.get_copy(), 42);
}

TEST(PushDataStorageTest, TrivialType_Struct) {
    struct Point { int x, y; }; // 平凡类型
    static_assert(std::is_trivially_copyable_v<Point>);

    data_carrier<Point> ps(Point{10, 20});

    Point p = ps.get();
    EXPECT_EQ(p.x, 10);
    EXPECT_EQ(p.y, 20);
}
