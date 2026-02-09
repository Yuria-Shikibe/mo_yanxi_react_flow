module;

#include <cassert>
#include <mo_yanxi/enum_operator_gen.hpp>
#include <mo_yanxi/adapted_attributes.hpp>


export module mo_yanxi.react_flow.data_storage;

import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{
	export enum struct data_state : std::uint8_t{
		fresh,
		expired,
		awaiting,
		failed
	};


	ENUM_COMPARISON_OPERATORS(data_state, export)

	export enum struct async_type : std::uint8_t{
		/**
		 * @brief When a new update is arrived and the previous is not done yet, cancel the previous
		 */
		async_latest,

		/**
		 * @brief When a new update is arrived and the previous is not done yet, just dispatch again
		 */
		async_all,

		def = async_latest
	};

	export enum struct trigger_type : std::uint8_t{
		disabled,
		on_pulse,
		active
	};

	export enum struct propagate_type : std::uint8_t{
		eager,
		lazy,
		pulse
	};

	export enum struct data_pending_state : std::uint8_t{
		done,
		expired,
		waiting_pulse
	};




	template <std::ranges::input_range Rng>
		requires (std::is_scoped_enum_v<std::ranges::range_value_t<Rng>>)
	std::ranges::range_value_t<Rng> merge_data_state(const Rng& states) noexcept{
		return std::ranges::max(states, std::ranges::less{}, [](const auto v){
			return std::to_underlying(v);
		});
	}

	export
	template <typename T>
	void update_state_enum(T& state, T other) noexcept{
		state = T{std::max(std::to_underlying(state), std::to_underlying(other))};
	}

	struct nit_{};

	export
	template <typename T>
	struct request_result{
		using value_type = T;

	private:
		union{
			T value_;
			nit_ uninit_{};
		};

		enum internal_data_state_ : std::underlying_type_t<data_state>{
			fresh,
			expired,
			awaiting,
			failed,
			expired_with_data_
		} state_;

		// 辅助：检查内部状态是否持有数据
		constexpr bool has_internal_value_() const noexcept{
			return state_ == fresh || state_ == expired_with_data_;
		}

		template <bool hasValue>
		static constexpr internal_data_state_ convert_state_(data_state ds) noexcept{
			if constexpr(hasValue){
				// 有值时，状态不应该是 awaiting 或 failed
				assert(ds != data_state::awaiting && ds != data_state::failed);
				if(ds == data_state::expired){
					return expired_with_data_;
				}
			} else{
				// 无值时，状态不应该是 fresh
				assert(ds != data_state::fresh);
			}
			return internal_data_state_{std::to_underlying(ds)};
		}

		constexpr void try_destruct_() noexcept{
			if(has_internal_value_()){
				value_.~value_type();
			}
		}

	public:
		// ========================================================================
		// 构造函数 (Constructors)
		// ========================================================================

		template <typename... Args>
			requires (std::constructible_from<value_type, Args&&...>)
		[[nodiscard]] constexpr request_result(bool isExpired,
			Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args&&...>)
			: value_(std::forward<Args>(args)...), state_(isExpired ? expired_with_data_ : fresh){
		}

		[[nodiscard]] constexpr request_result(data_state result)
			: state_(convert_state_<false>(result)){
		}

		constexpr ~request_result(){
			try_destruct_();
		}

		// 1. 拷贝构造函数
		constexpr request_result(const request_result& other)
			requires (std::is_copy_constructible_v<T>)
			: state_(other.state_){
			if(other.has_internal_value_()){
				std::construct_at(&value_, other.value_);
			}
		}

		// 2. 移动构造函数
		constexpr request_result(request_result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
			requires (std::is_move_constructible_v<T>)
			: state_(other.state_){
			if(other.has_internal_value_()){
				std::construct_at(&value_, std::move(other.value_));
			}
		}

		// ========================================================================
		// 赋值操作符 (Assignment Operators)
		// ========================================================================

		// 3. 拷贝赋值
		constexpr request_result& operator=(const request_result& other)
			requires (std::is_copy_constructible_v<T> && std::is_copy_assignable_v<T>){
			if(this == &other) return *this;

			if(other.has_internal_value_()){
				if(this->has_internal_value_()){
					// Case 1: 都有值 -> 直接赋值
					this->value_ = other.value_;
					this->state_ = other.state_;
				} else{
					// Case 2: 他有值，我无值 -> 构造我
					std::construct_at(&this->value_, other.value_);
					this->state_ = other.state_;
				}
			} else{
				if(this->has_internal_value_()){
					// Case 3: 他无值，我有值 -> 析构我
					this->try_destruct_();
				}
				// Case 4: 都无值 -> 仅拷贝状态
				this->state_ = other.state_;
			}
			return *this;
		}

		// 4. 移动赋值
		constexpr request_result& operator=(request_result&& other)
			noexcept(std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T>)
			requires (std::is_move_constructible_v<T> && std::is_move_assignable_v<T>){
			if(this == &other) return *this;

			if(other.has_internal_value_()){
				if(this->has_internal_value_()){
					this->value_ = std::move(other.value_);
					this->state_ = other.state_;
				} else{
					std::construct_at(&this->value_, std::move(other.value_));
					this->state_ = other.state_;
				}
			} else{
				if(this->has_internal_value_()){
					this->try_destruct_();
				}
				this->state_ = other.state_;
			}
			return *this;
		}

		// ========================================================================
		// 基础观察器 (Basic Observers)
		// ========================================================================

		constexpr bool has_value() const noexcept{
			// 对外暴露的 has_value 语义：只要不是 awaiting 或 failed 就算有值（包括 fresh 和 expired）
			return state_ == fresh || state_ == expired_with_data_;
		}

		constexpr data_state state() const noexcept{
			switch(state_){
			case fresh : return data_state::fresh;
			case expired : return data_state::expired; // 实际上如果是 expired_with_data_ 也会进这里
			case awaiting : return data_state::awaiting;
			case failed : return data_state::failed;
			case expired_with_data_ : return data_state::expired;
			}
			std::unreachable();
		}

		constexpr explicit operator bool() const noexcept{
			return has_value();
		}

		// C++23 Deducing This Accessors
		template <typename S>
		constexpr auto&& value(this S&& self){
			if(!self.has_value()){
				throw std::bad_optional_access{};
			}
			return std::forward_like<S>(self.value_);
		}

		template <typename S>
		constexpr auto&& value_unchecked(this S&& self) noexcept{
			assert(self.has_value());
			return std::forward_like<S>(self.value_);
		}

		// ========================================================================
		// 类似 Optional 的扩展函数 (Optional-like Extensions)
		// ========================================================================

		// value_or: 如果有值返回引用/拷贝，否则返回默认值
		template <typename S, typename U>
			requires (std::constructible_from<T, U&&>)
		constexpr T value_or(this S&& self, U&& default_value)
			noexcept(
				std::is_nothrow_constructible_v<T, U&&> &&
				std::is_nothrow_constructible_v<T, decltype(std::forward_like<S>(self.value_))>)
			requires (std::convertible_to<U, T> && std::copy_constructible<T>){
			return self.has_value()
				       ? std::forward_like<S>(self.value_)
				       : static_cast<T>(std::forward<U>(default_value));
		}

		// emplace: 原地构造新值，并设置状态为 fresh
		template <typename... Args>
			requires (std::constructible_from<T, Args&&...>)
		constexpr T& emplace(Args&&... args) noexcept(std::is_nothrow_constructible_v<T, Args&&...>){
			try_destruct_();
			std::construct_at(&value_, std::forward<Args>(args)...);
			state_ = fresh;
			return value_;
		}

		// reset: 清除值并设置为指定状态 (默认为 failed)
		// 注意：不能 reset 为 fresh 或 expired_with_data，因为那需要值
		constexpr void reset(data_state new_state = data_state::failed) noexcept{
			assert(new_state != data_state::fresh); // 不能 reset 成需要值的状态而不给值
			try_destruct_();
			// 简单映射，因为这里确信没有值
			state_ = static_cast<internal_data_state_>(std::to_underlying(new_state));
		}

		// ========================================================================
		// 比较操作符 (Comparison Operators)
		// ========================================================================

		// 1. 与另一个 request_result 比较
		// 逻辑：状态必须完全一致。如果有值，值也必须一致。
		friend constexpr bool operator==(const request_result& lhs,
			const request_result& rhs) noexcept(noexcept(lhs.value_ == rhs.value_))
			requires (std::equality_comparable<T>){
			if(lhs.state_ != rhs.state_){
				return false;
			}
			// 状态相同。如果是有值状态，比较值。
			if(lhs.has_internal_value_()){
				return lhs.value_ == rhs.value_;
			}
			// 都是无值状态且状态相同 (如都为 awaiting)
			return true;
		}

		friend constexpr bool operator==(const request_result& lhs, const T& rhs) noexcept(noexcept(lhs.value_ == rhs))
			requires (std::equality_comparable<T>){
			return lhs.has_value() && lhs.value_ == rhs;
		}

		friend constexpr bool operator==(const T& lhs, const request_result& rhs) noexcept(noexcept(lhs == rhs.value_))
			requires (std::equality_comparable<T>){
			return rhs.has_value() && lhs == rhs.value_;
		}

		friend constexpr bool operator==(const request_result& lhs, data_state rhs) noexcept{
			return lhs.state() == rhs;
		}
	};
}

namespace mo_yanxi::react_flow{
	export
	struct data_carrier_obj;

	export
	template <typename T>
	class data_carrier;

	// -----------------------------------------------------------------------------
	// 特化版本 1: 当 T 不是平凡可复制类型 (Non-Trivially Copyable)
	// 使用 variant<monostate, const T*, T>
	// -----------------------------------------------------------------------------
	template <typename T>
		requires (!std::is_trivially_copyable_v<T>)
	class data_carrier<T>{
	private:
		using variant_t = std::variant<std::monostate, const T*, T>;
	public:
		using value_type = T;
		static constexpr bool is_trivial = false;

		// 构造函数：默认初始化 (monostate)
		constexpr data_carrier() = default;

		// 构造函数：直接移动传入 T
		constexpr explicit(false) data_carrier(T&& val) : storage_(std::move(val)){
		}

		// 构造函数：传入 const T*
		constexpr explicit(false) data_carrier(const T& ptr) : storage_(&ptr){
		}

		// 移动构造和赋值
		constexpr data_carrier(data_carrier&& other) noexcept(std::is_nothrow_move_constructible_v<T>) requires(std::is_move_constructible_v<T>) : storage_(std::exchange(other.storage_, std::monostate{})){}
		constexpr data_carrier& operator=(data_carrier&& other) noexcept(std::is_nothrow_move_assignable_v<T>) requires(std::is_move_assignable_v<T>){
			storage_ = std::exchange(other.storage_, std::monostate{});
			return *this;
		}

		constexpr data_carrier(const data_carrier&) noexcept(std::is_nothrow_copy_constructible_v<T>) requires(std::is_copy_constructible_v<T>) = default;
		constexpr data_carrier& operator=(const data_carrier&) noexcept(std::is_nothrow_copy_constructible_v<T>) requires(std::is_copy_constructible_v<T>) = default;


		constexpr const T* get_view() const noexcept{
			return std::visit<const T*>([]<typename Ty>(const Ty& input){
				if constexpr (std::same_as<Ty, std::monostate>){
					return nullptr;
				}else if constexpr(std::same_as<Ty, const T*>){
					return input;
				}else{
					return std::addressof(input);
				}
			}, storage_);
		}

		constexpr const T& get_ref_view() const{
			return std::visit<const T&>([]<typename Ty>(const Ty& input) -> const T& {
				if constexpr (std::same_as<Ty, std::monostate>){
					throw std::bad_variant_access{};
				}else if constexpr(std::same_as<Ty, const T*>){
					return *input;
				}else{
					return input;
				}
			}, storage_);
		}

		[[nodiscard]] constexpr T get(){
			if(std::holds_alternative<T>(storage_)){
				T ret = std::move(std::get<T>(storage_));
				storage_ = std::monostate{};
				return ret;
			}

			if(std::holds_alternative<const T*>(storage_)){
				const T* ptr = std::get<const T*>(storage_);
				assert(ptr != nullptr);
				return *ptr;
			}

			throw std::runtime_error("w is empty");
		}

		[[nodiscard]] constexpr T get_copy() const noexcept(std::is_nothrow_copy_constructible_v<T>){
			if(std::holds_alternative<T>(storage_)){
				return std::get<T>(storage_);
			}

			if(std::holds_alternative<const T*>(storage_)){
				const T* ptr = std::get<const T*>(storage_);
				assert(ptr != nullptr);
				return *ptr;
			}

			throw std::runtime_error("w is empty");
		}

		[[nodiscard]] constexpr bool is_empty() const{
			return std::holds_alternative<std::monostate>(storage_);
		}

		constexpr explicit operator bool() const noexcept{
			return !is_empty();
		}

		explicit(false) constexpr operator data_carrier_obj&() noexcept {
			return reinterpret_cast<data_carrier_obj&>(*this);
		}

		explicit(false) constexpr operator data_carrier_obj&&() &&noexcept {
			return reinterpret_cast<data_carrier_obj&&>(*this);
		}

		constexpr bool tobe_moved() const noexcept{
			return std::holds_alternative<T>(storage_);
		}

	private:
		variant_t storage_{};
	};

	// -----------------------------------------------------------------------------
	// 特化版本 2: 当 T 是平凡可复制类型 (Trivially Copyable)
	// 直接持有 T，不使用 Variant
	// -----------------------------------------------------------------------------
	template <typename T>
		requires std::is_trivially_copyable_v<T> && std::is_object_v<T>
	class data_carrier<T>{
	public:
		using value_type = T;
		static constexpr bool is_trivial = true;
		// 构造函数：值初始化
		constexpr data_carrier() : value_{}{
		}

		constexpr explicit(false) data_carrier(const T& val) : value_(val){
		}

		constexpr data_carrier(const data_carrier&) = default;
		constexpr data_carrier& operator=(const data_carrier&) = default;
		constexpr data_carrier(data_carrier&&) = default;
		constexpr data_carrier& operator=(data_carrier&&) = default;

		constexpr const T* get_view() const noexcept{
			return std::addressof(value_);
		}

		constexpr const T& get_ref_view() const noexcept{
			return value_;
		}

		[[nodiscard]] constexpr T get() const noexcept {
			return value_;
		}

		[[nodiscard]] constexpr T get_copy() const noexcept{
			return value_;
		}

		explicit(false) constexpr operator data_carrier_obj&() noexcept {
			return reinterpret_cast<data_carrier_obj&>(*this);
		}

		explicit(false) constexpr operator data_carrier_obj&&() && noexcept {
			return reinterpret_cast<data_carrier_obj&&>(*this);
		}

		constexpr bool tobe_moved() const noexcept{
			return false; //never get invalid after move on a trivial object
		}

		[[nodiscard]] constexpr bool is_empty() const{
			return false;
		}

		constexpr explicit operator bool() const noexcept{
			return !is_empty();
		}

	private:
		T value_;
	};

	export
	template <typename T>
	using data_pass_t =
		std::conditional_t<data_carrier<T>::is_trivial,
			std::conditional_t<(sizeof(T) > sizeof(void*) * 2), const T&, T>, data_carrier<T>&>;

	export
	template <typename T>
		requires (data_carrier<T>::is_trivial && sizeof(T) <= sizeof(void*) * 2)
	constexpr data_pass_t<T> pass_data(data_carrier<T>& input) noexcept {
		return input.get();
	}

	export
	template <typename T>
		requires (data_carrier<T>::is_trivial && sizeof(T) > sizeof(void*) * 2)
	constexpr data_pass_t<T> pass_data(data_carrier<T>& input) noexcept {
		return input.get();
	}

	export
	template <typename T>
		requires (!data_carrier<T>::is_trivial)
	constexpr data_pass_t<T> pass_data(data_carrier<T>& input) noexcept {
		return input;
	}

	export
	template <typename T>
		requires (std::is_trivially_copyable_v<T> && std::is_object_v<T>)
	data_carrier(const T&) -> data_carrier<T>;

	export
	template <typename T>
	requires (!std::is_trivially_copyable_v<std::remove_cvref_t<T>>)
	data_carrier(T&&) -> data_carrier<std::remove_cvref_t<T>>;

	export
	template <typename T>
	data_carrier<T>& push_data_cast(data_carrier_obj& obj) noexcept{
		return reinterpret_cast<data_carrier<T>&>(obj);
	}

	export
	template <typename T>
	const data_carrier<T>& push_data_cast(const data_carrier_obj& obj) noexcept{
		return reinterpret_cast<const data_carrier<T>&>(obj);
	}

	export
	template <typename T>
	data_carrier<T>&& push_data_cast(data_carrier_obj&& obj) noexcept{
		return reinterpret_cast<data_carrier<T>&&>(obj);
	}

	export
	template <typename T>
	const data_carrier<T>&& push_data_cast(const data_carrier_obj&& obj) noexcept{
		return reinterpret_cast<const data_carrier<T>&&>(obj);
	}



	//TODO return both data and state
	export
	template <typename T>
	using request_pass_handle = request_result<data_carrier<T>>;

	export
	template <typename T>
	[[nodiscard]] request_pass_handle<T> make_request_handle_unexpected(data_state error) noexcept{
		return request_pass_handle<T>{error};
	}

	export
	template <typename T>
	[[nodiscard]] request_pass_handle<std::decay_t<T>> make_request_handle_expected(T&& data, bool isExpired) noexcept{
		return request_pass_handle<std::decay_t<T>>(isExpired, std::forward<T>(data));
	}

	export
	template <typename T>
	[[nodiscard]] request_pass_handle<T> make_request_handle_expected_ref(const T& data, bool isExpired) noexcept{
		return request_pass_handle<T>(isExpired, data);
	}

	export
	template <typename T>
	[[nodiscard]] request_pass_handle<T> make_request_handle_expected_from_data_storage(data_carrier<T>&& data, bool isExpired) noexcept{
		return request_pass_handle<T>(isExpired, std::move(data));
	}


}

namespace mo_yanxi::react_flow{
	export
	template <typename  T, bool has_it = false>
	struct optional_val{
		using value_type = T;

		optional_val& operator=(const T& val) noexcept{
			return *this;
		}

		optional_val& operator=(T&& val) noexcept{
			return *this;
		}
	};


	export
	template <typename T>
	struct optional_val<T, true>{
		using value_type = T;
		T value;

		const T& operator*() const noexcept{
			return value;
		}

		T& operator*() noexcept{
			return value;
		}

		optional_val& operator=(const T& val) noexcept(std::is_nothrow_copy_assignable_v<T>){
			value = val;
			return *this;
		}

		optional_val& operator=(T&& val) noexcept(std::is_nothrow_move_assignable_v<T>){
			value = std::move(val);
			return *this;
		}
	};

	export
	struct descriptor_tag{
		/**
		 * @brief enable node-local cache
		 */
		bool cache;

		/**
		 * @brief if set, node won't push forward when relevant data is updated
		 */
		bool quiet;

		/**
		 * @brief disallow expired on fetch request
		 */
		bool fresh;
	};

	static_assert(std::ranges::view<std::span<int>>);

	template <typename S, typename D>
		requires (std::convertible_to<S, D>)
	struct convertor{

		FORCE_INLINE static D operator()(data_carrier<S>&& input) requires (!std::ranges::view<D> && !std::same_as<S, D>){
			return input.get();
		}

		FORCE_INLINE static D operator()(data_carrier<S>&& input) requires (std::ranges::view<D> && !std::same_as<S, D>){
			return input.get_ref_view();
		}

		FORCE_INLINE static data_carrier<D>&& operator()(data_carrier<S>&& input) noexcept requires (std::same_as<S, D>){
			return std::move(input);
		}
	};

	export
	template <typename InputType, descriptor_tag Tag = {}, typename OutputType = InputType, typename ConvertorTy = convertor<InputType, OutputType>>
		requires requires(data_carrier<InputType>&& input, const ConvertorTy& transformer){
			{ std::invoke(transformer, std::move(input)) } -> std::convertible_to<data_carrier<OutputType>>;
		}
	struct EMPTY_BASE descriptor{
		using input_type = InputType;
		using output_type = OutputType;
		using convertor_type = ConvertorTy;
		static constexpr descriptor_tag tag = Tag;

		//TODO check std::ref/const_ref?
		static_assert(!(std::ranges::view<OutputType> && !tag.cache), "view from input must have it cached, or it will casue dangling");
		static_assert(std::convertible_to<std::invoke_result_t<convertor_type, data_carrier<input_type>&&>, data_carrier<output_type>>);
		static_assert(std::is_object_v<InputType>);

	private:
		ADAPTED_NO_UNIQUE_ADDRESS optional_val<input_type, tag.cache> value;
		ADAPTED_NO_UNIQUE_ADDRESS convertor_type transformer;

	public:
		constexpr descriptor() = default;
		explicit constexpr descriptor(const convertor_type& transformer) : transformer(transformer){}
		explicit constexpr descriptor(convertor_type&& transformer) : transformer(std::move(transformer)){}

		FORCE_INLINE constexpr const convertor_type& get_transformer() const noexcept{
			return transformer;
		}

		FORCE_INLINE constexpr data_carrier<output_type> get() const noexcept(std::is_nothrow_invocable_r_v<output_type, const convertor_type&, const input_type&>) requires(tag.cache){
			return std::invoke(transformer, data_carrier{*value});
		}

		// FORCE_INLINE constexpr data_carrier<output_type> get(data_carrier<input_type>& pushed) const noexcept(std::is_nothrow_invocable_r_v<output_type, const convertor_type&, input_type>){
		// 	return std::invoke(transformer, pushed);
		// }


		FORCE_INLINE constexpr data_carrier<output_type> get(data_carrier<input_type>&& pushed) const noexcept(std::is_nothrow_invocable_r_v<output_type, const convertor_type&, input_type>){
			return std::invoke(transformer, std::move(pushed));
		}


		FORCE_INLINE output_type operator*() const requires(tag.cache){
			return get().get();
		}


		constexpr data_carrier<output_type> operator<<(data_carrier<input_type>&& pushed){
			if constexpr (tag.cache){
				*value = pushed.get();
				return this->get();
			}else{
				return this->get(std::move(pushed));
			}
		}

		constexpr void set(data_carrier<input_type>& pushed) requires(tag.cache){
			*value = pushed.get();
		}

		constexpr void set(data_carrier<input_type>&& pushed) requires(tag.cache){
			this->set(pushed);
		}

		constexpr void set(const input_type& pushed) requires(tag.cache){
			*value = pushed;
		}

		constexpr void set(input_type&& pushed) requires(tag.cache){
			*value = std::move(pushed);
		}
	};

	template <typename T>
	struct is_spec_of_descriptor : std::false_type{};


	template <typename InputType, descriptor_tag Tag, typename Trans, typename Transf>
	struct is_spec_of_descriptor<descriptor<InputType, Tag, Trans, Transf>> : std::true_type{};

	export
	template <typename T>
	concept spec_of_descriptor = is_spec_of_descriptor<T>::value;

	export
	template <spec_of_descriptor T>
	struct descriptor_trait{
		using input_type = T::input_type;
		using output_type = T::output_type;
		using convertor_type = T::convertor_type;

		using input_pass_type = data_carrier<input_type>;
		using output_pass_type = data_carrier<output_type>;

		using operator_pass_type = data_pass_t<output_type>;
		static constexpr descriptor_tag tag = T::tag;
		static constexpr bool cached = tag.cache;
		static constexpr bool identity = std::same_as<input_type, output_type>;
		static constexpr bool allow_expired = !tag.fresh;
		static constexpr bool no_push = tag.quiet;
	};

	/*
	template <typename ...Args>
		requires (spec_of_descriptor<Args> && ...)
	struct argument_descriptor{
		using args_descriptor_tuple = std::tuple<Args...>;
		static constexpr std::size_t cached_count = (static_cast<std::size_t>(descriptor_trait<Args>::cached) + ... + 0);

		args_descriptor_tuple arguments_;
		ADAPTED_NO_UNIQUE_ADDRESS std::bitset<cached_count> dirty_flags_;

		bool is_dirty() const noexcept{
			return dirty_flags_.any();
		}
	};*/

	template <std::size_t N>
	consteval std::array<smallest_uint_t<N>, N> make_index_array(const bool(& input)[N]){
		smallest_uint_t<N> sz{};
		std::array<smallest_uint_t<N>, N> rst;
		for(std::size_t i = 0; i < N; ++i){
			if(input[i]){
				rst[i] = sz;
				++sz;
			}else{
				rst[i] = std::numeric_limits<smallest_uint_t<N>>::max();
			}
		}
		return rst;
	}

	export
	template <bool... cache_bit>
	struct dirty_bits{
		static constexpr std::size_t total_bits = sizeof...(cache_bit);
		static constexpr std::array<smallest_uint_t<total_bits>, total_bits> indices{react_flow::make_index_array({cache_bit...})};
		static constexpr std::size_t cached_count = (static_cast<std::size_t>(cache_bit) + ... + 0uz);

	private:
		ADAPTED_NO_UNIQUE_ADDRESS std::bitset<cached_count> dirty_flags;

	public:
		FORCE_INLINE static constexpr bool has_bit(std::size_t index) noexcept{
			return index < total_bits && indices[index] != std::numeric_limits<smallest_uint_t<total_bits>>::max();
		}

		FORCE_INLINE constexpr void clear() noexcept{
			dirty_flags = {};
		}

		FORCE_INLINE constexpr bool is_dirty() const noexcept{
			return dirty_flags.any();
		}

		FORCE_INLINE constexpr bool check_requires_reload(std::size_t slot){
			if(!has_bit(slot))return true;
			return !get(slot);
		}

		FORCE_INLINE constexpr bool get(std::size_t slot) const noexcept{
			return dirty_flags.test(indices[slot]);
		}

		FORCE_INLINE constexpr void set(std::size_t slot, bool t = true) noexcept{
			dirty_flags.set(indices[slot], t);
		}

		template <std::size_t Idx>
			requires (has_bit(Idx))
		FORCE_INLINE constexpr void set(bool t = true) noexcept{
			dirty_flags.set(indices[Idx], t);
		}

		FORCE_INLINE constexpr bool try_set(std::size_t slot) noexcept{
			if(!has_bit(slot))return true;
			if(!get(slot)){
				dirty_flags.set(indices[slot]);
				return true;
			}
			return false;
		}
	};

}