module;

#include <mo_yanxi/adapted_attributes.hpp>

#if defined(__GNUC__) || defined(__clang__)
	#define NO_INLINE [[gnu::noinline]]
#elif _MSC_VER
	#define NO_INLINE [[msvc::noinline]]
#else
	#define NO_INLINE
#endif

export module mo_yanxi.react_flow:successory_list;

import std;
import :node_interface;


namespace mo_yanxi::react_flow{

	export struct successor_list {
		static constexpr std::size_t sso_count = 2;

		using value_type = successor_entry;
		using size_type = std::size_t;
		using reference = value_type&;
		using const_reference = const value_type&;
		using pointer = value_type*;
		using const_pointer = const value_type*;

		using iterator = value_type*;
		using const_iterator = const value_type*;

	private:

		union storage_t {
			std::array<value_type, sso_count> stack;
			std::vector<value_type> heap;

			storage_t() {}
			~storage_t(){}
		};

		storage_t storage_;
		size_type size_ = 0;
		bool using_heap_ = false;


	public:
		successor_list() noexcept {
			std::construct_at(&storage_.stack);
		}

		~successor_list() {
			if (using_heap_) {
				std::destroy_at(&storage_.heap);
			} else {
				std::destroy_at(&storage_.stack);
			}
		}

		successor_list(const successor_list& other) = delete;
		successor_list& operator=(const successor_list& other) = delete;

		successor_list(successor_list&& other) noexcept : size_(other.size_), using_heap_(other.using_heap_) {
			if (other.using_heap_) {
				std::construct_at(&storage_.heap, std::move(other.storage_.heap));
			} else {
				std::construct_at(&storage_.stack);
				for (size_type i = 0; i < other.size_; ++i) {
					storage_.stack[i] = std::move(other.storage_.stack[i]);
				}
			}
			other.size_ = 0;
		}

		successor_list& operator=(successor_list&& other) noexcept {
			if (this == &other) return *this;

			if (using_heap_) {
				std::destroy_at(&storage_.heap);
			} else {
				std::destroy_at(&storage_.stack);
			}

			using_heap_ = other.using_heap_;
			size_ = other.size_;

			if (using_heap_) {
				std::construct_at(&storage_.heap, std::move(other.storage_.heap));
			} else {
				std::construct_at(&storage_.stack);
				for (size_type i = 0; i < size_; ++i) {
					storage_.stack[i] = std::move(other.storage_.stack[i]);
				}
			}

			other.size_ = 0;

			return *this;
		}

		[[nodiscard]] iterator begin() noexcept {
			if (using_heap_) return storage_.heap.data();
			return storage_.stack.data();
		}

		[[nodiscard]] iterator end() noexcept {
			return begin() + size_;
		}

		[[nodiscard]] const_iterator begin() const noexcept {
			if (using_heap_) return storage_.heap.data();
			return storage_.stack.data();
		}

		[[nodiscard]] const_iterator end() const noexcept {
			return begin() + size_;
		}

		[[nodiscard]] bool empty() const noexcept {
			return size_ == 0;
		}

		[[nodiscard]] size_type size() const noexcept {
			return size_;
		}

		void clear() noexcept {
			if (using_heap_) {
				storage_.heap.clear();
			} else {
				if (!std::is_trivially_destructible_v<value_type>) {
					for (size_type i = 0; i < size_; ++i) {
						storage_.stack[i] = value_type{};
					}
				}
			}
			size_ = 0;
		}

		template <typename... Args>
		reference emplace_back(Args&&... args) {
			if (using_heap_) {
				size_++;
				return storage_.heap.emplace_back(std::forward<Args>(args)...);
			} else {
				if (size_ < sso_count) {
					reference val = storage_.stack[size_];
					val = value_type(std::forward<Args>(args)...);
					size_++;
					return storage_.stack[size_ - 1];
				} else {
					switch_to_heap();
					size_++;
					return storage_.heap.emplace_back(std::forward<Args>(args)...);
				}
			}
		}

		void push_back(value_type&& val){
			emplace_back(std::move(val));
		}

		void push_back(const value_type& val){
			emplace_back(val);
		}

		// ------------------------------------------------------------------------
		// Global Friend: Swap-and-Pop Erase
		// ------------------------------------------------------------------------
		template <typename Pred>
		friend size_type erase_if(successor_list& c, Pred pred) noexcept {
			size_type removed_count = 0;
			pointer p_begin = c.begin();

			for (size_type i = 0; i < c.size_; ) {
				if (pred(p_begin[i])) {
					// Swap with last if not already last
					if (i != c.size_ - 1) {
						p_begin[i] = std::move(p_begin[c.size_ - 1]);
					}

					// Pop back logic
					if (c.using_heap_) {
						c.storage_.heap.pop_back();
					} else {
						// Destruct/Reset stack element
						if (!std::is_trivially_destructible_v<value_type>) {
							p_begin[c.size_ - 1] = value_type{};
						}
					}

					c.size_--;
					removed_count++;
					// Re-evaluate current index (it now holds the swapped element)
				} else {
					++i;
				}
			}
			return removed_count;
		}

	private:
		NO_INLINE void switch_to_heap() {
			std::vector<value_type> new_heap;
			new_heap.reserve(sso_count + 1);

			for (size_type i = 0; i < size_; ++i) {
				new_heap.push_back(std::move(storage_.stack[i]));
			}

			std::destroy_at(&storage_.stack);
			std::construct_at(&storage_.heap, std::move(new_heap));
			using_heap_ = true;
		}
	};


	bool try_insert(successor_list& successors, std::size_t slot, node& next){
		if(std::ranges::find_if(successors, [&](const successor_entry& e){
			return e.index == slot && e.entity == &next;
		}) != successors.end()){
			return false;
		}

		successors.emplace_back(slot, next);
		return true;
	}

	bool try_erase(successor_list& successors, const std::size_t slot, const node& next) noexcept{
		return erase_if(successors, [&](const successor_entry& e){
			return e.index == slot && e.entity == &next;
		});
	}
}
