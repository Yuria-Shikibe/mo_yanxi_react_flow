module;
#include <mutex>
#include <utility>

export module mo_yanxi.concurrent.atomic_double_buffer;

import std;

namespace mo_yanxi::ccur {

    export
    template <typename T>
    struct atomic_double_buffer {
    private:
        T data_;
        std::mutex mtx_;

    public:
        atomic_double_buffer() = default;

        template <typename Fn>
        void modify(Fn&& fn) {
            std::lock_guard<std::mutex> lock(mtx_);
            fn(data_);
        }

        template <typename Fn>
        void load(Fn&& fn) {
            std::lock_guard<std::mutex> lock(mtx_);
            fn(data_);
        }
    };
}
