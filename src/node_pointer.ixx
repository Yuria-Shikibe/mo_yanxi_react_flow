module;
#include <cassert>

export module mo_yanxi.react_flow:node_pointer;
import :node_interface;
import std;

namespace mo_yanxi::react_flow{

	export
	struct node_pointer{
		using pointer = node*;
		using element_type = node;

		constexpr node_pointer() noexcept = default;

		constexpr node_pointer(std::nullptr_t) noexcept{
		}

		explicit node_pointer(pointer p) : ptr_(p){
			if(ptr_){
				ptr_->add_ref();
			}
		}

		node_pointer(const node_pointer& other) : ptr_(other.ptr_){
			if(ptr_){
				ptr_->add_ref();
			}
		}

		node_pointer(node_pointer&& other) noexcept : ptr_(other.ptr_){
			other.ptr_ = nullptr;
		}

		template <typename T, typename... Args>
			requires (std::constructible_from<T, Args&&...>)
		explicit node_pointer(std::in_place_type_t<T>, Args&&... args)
			: ptr_(new T(std::forward<Args>(args)...)){
			if(ptr_){
				ptr_->add_ref();
			}
		}

		~node_pointer(){
			if(ptr_){
				ptr_->release();
			}
		}

		node_pointer& operator=(const node_pointer& other){
			if(this != &other){
				if(ptr_){
					ptr_->release();
				}
				ptr_ = other.ptr_;
				if(ptr_){
					ptr_->add_ref();
				}
			}
			return *this;
		}

		node_pointer& operator=(node_pointer&& other) noexcept{
			if(this != &other){
				if(ptr_){
					ptr_->release();
				}
				ptr_ = other.ptr_;
				other.ptr_ = nullptr;
			}
			return *this;
		}

		node_pointer& operator=(std::nullptr_t){
			if(ptr_){
				ptr_->release();
			}
			ptr_ = nullptr;
			return *this;
		}

		[[nodiscard]] explicit operator bool() const noexcept{
			return ptr_ != nullptr;
		}

		[[nodiscard]] pointer get() const noexcept{
			return ptr_;
		}

		[[nodiscard]] pointer operator->() const noexcept{
			assert(ptr_ != nullptr);
			return ptr_;
		}

		[[nodiscard]] element_type& operator*() const noexcept{
			assert(ptr_ != nullptr);
			return *ptr_;
		}

		[[nodiscard]] operator pointer() const noexcept{
			return ptr_;
		}

		void swap(node_pointer& other) noexcept{
			std::swap(ptr_, other.ptr_);
		}

		friend void swap(node_pointer& lhs, node_pointer& rhs) noexcept{
			lhs.swap(rhs);
		}

		friend bool operator==(const node_pointer& lhs, const node_pointer& rhs) noexcept{
			return lhs.ptr_ == rhs.ptr_;
		}

		friend bool operator==(const node_pointer& lhs, std::nullptr_t) noexcept{
			return lhs.ptr_ == nullptr;
		}

	protected:
		pointer ptr_ = nullptr;
	};

	export
	template <typename T>
	struct typed_node_pointer : node_pointer{
		using node_pointer::node_pointer;

		typed_node_pointer(node_pointer&& other) noexcept : node_pointer(std::move(other)){
		}

		typed_node_pointer(const node_pointer& other) : node_pointer(other){
		}

		[[nodiscard]] T* operator->() const noexcept{
			return static_cast<T*>(ptr_);
		}

		[[nodiscard]] T& operator*() const noexcept{
			return static_cast<T&>(*ptr_);
		}

		[[nodiscard]] T* get() const noexcept{
			return static_cast<T*>(ptr_);
		}

		[[nodiscard]] operator T*() const noexcept{
			return static_cast<T*>(ptr_);
		}
	};

}
