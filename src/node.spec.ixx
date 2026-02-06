module;

#include <cassert>

export module mo_yanxi.react_flow:nodes;

import :manager;
import :node_interface;
import :async_nodes;

import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{
	export
	template <typename T>
	struct provider_cached : provider_general<T>{
		using value_type = T;
		static_assert(std::is_object_v<value_type>);

	private:
		T data_{};

	public:
		[[nodiscard]] provider_cached() = default;

		[[nodiscard]] explicit provider_cached(manager& manager)
			: provider_general<T>(manager){
		}

		[[nodiscard]] provider_cached(manager& manager, propagate_behavior propagate_type)
			: provider_general<T>(manager, propagate_type){
		}

		void update_value(T&& value){
			data_ = std::move(value);
			on_update();
		}

		void update_value(const T& value){
			data_ = value;
			on_update();
		}

		template <bool check_equal = false, std::invocable<T&> Proj, typename Ty>
			requires (std::assignable_from<std::invoke_result_t<Proj, T&>, Ty&&>)
		void update_value(Proj proj, Ty&& value){
			auto& val = std::invoke(std::move(proj), data_);
			if constexpr(check_equal){
				if(val == value){
					return;
				}
			}

			val = std::forward<Ty>(value);
			on_update();
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			return data_state::fresh;
		}

		request_pass_handle<T> request_raw(bool allow_expired) override{
			//source is always fresh
			return react_flow::make_request_handle_expected_ref(data_, false);
		}

	protected:
		void on_push(void* in_data) override{
			T& target = *static_cast<T*>(in_data);
			data_ = std::move(target);
			on_update();
		}

		void on_push(manager& manager, std::size_t, push_data_obj& in_data) override{
			auto& storage = push_data_cast<T>(in_data);
			data_ = storage.get_copy();
			on_update();
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;
			push_data_storage<T> data(data_);
			for(const successor_entry& successor : this->successors){
				successor.update(m, data);
			}
		}

	private:
		void on_update(){
			assert(this->manager_ != nullptr);
			switch(this->data_propagate_type_){
			case propagate_behavior::eager : this->data_pending_state_ = data_pending_state::done;
				{
					push_data_storage<T> data(data_);
					for(const successor_entry& successor : this->successors){
						successor.update(*this->manager_, data);
					}
				}
				break;
			case propagate_behavior::lazy : this->data_pending_state_ = data_pending_state::done;
				std::ranges::for_each(this->successors, &successor_entry::mark_updated);
				break;
			case propagate_behavior::pulse : this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			default : std::unreachable();
			}
		}
	};

	//TODO provider transient?

	template <typename Ret, typename... Args>
	struct modifier_base : type_aware_node<Ret>{
		static_assert((std::is_object_v<Args> && ...) && std::is_object_v<Ret>);

		static constexpr std::size_t arg_count = sizeof...(Args);
		static constexpr std::array<data_type_index, arg_count> in_type_indices{
				unstable_type_identity_of<Args>()...
			};
		using arg_type = std::tuple<std::remove_const_t<Args>...>;

	private:
		std::array<raw_node_ptr, arg_count> parents{};
		std::vector<successor_entry> successors{};

	public:
		[[nodiscard]] modifier_base() = default;

		[[nodiscard]] explicit modifier_base(propagate_behavior data_propagate_type)
			: type_aware_node<Ret>(data_propagate_type){
		}

		[[nodiscard]] bool is_isolated() const noexcept override{
			return std::ranges::none_of(parents, std::identity{}) && successors.empty();
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			data_state states{};

			for(const raw_node_ptr& p : parents){
				update_state_enum(states, p->get_data_state());
			}

			return states;
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{in_type_indices};
		}

		void disconnect_self_from_context() noexcept final{
			for(std::size_t i = 0; i < parents.size(); ++i){
				if(raw_node_ptr& ptr = parents[i]){
					ptr->erase_successors_single_edge(i, *this);
					ptr = nullptr;
				}
			}
			for(const auto& successor : successors){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors.clear();
		}

		[[nodiscard]] std::span<const raw_node_ptr> get_inputs() const noexcept final{
			return parents;
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors;
		}

	private:
		bool connect_successors_impl(std::size_t slot, node& post) final{
			if(auto& ptr = post.get_inputs()[slot]){
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return node::try_insert(successors, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return node::try_erase(successors, slot, post);
		}

		void connect_predecessor_impl(std::size_t slot, node& prev) final{
			if(parents[slot]){
				const auto rng = parents[slot]->get_outputs();
				const auto idx = std::ranges::distance(rng.begin(),
					std::ranges::find(rng, this, &successor_entry::get));
				parents[slot]->erase_successors_single_edge(idx, *this);
			}
			parents[slot] = std::addressof(prev);
		}

		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			if(parents[slot] == &prev){
				parents[slot] = {};
			}
		}

	protected:
		virtual void update_children(manager& manager, Ret& val) const{
			std::size_t count = this->successors.size();
			for(std::size_t i = 0; i < count; ++i){
				if(i == count - 1){
					push_data_storage<Ret> data(std::move(val));
					this->successors[i].update(manager, data);
				} else{
					push_data_storage<Ret> data(val);
					this->successors[i].update(manager, data);
				}
			}
		}

		Ret apply(arg_type&& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> Ret{
				return this->operator()(std::move(std::get<Idx>(arguments))...);
			}(std::index_sequence_for<Args...>());
		}

		Ret apply(const arg_type& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> Ret{
				return this->operator()(std::get<Idx>(arguments)...);
			}(std::index_sequence_for<Args...>());
		}

		virtual Ret operator()(Args&&... args){
			return this->operator()(std::as_const(args)...);
		}

		virtual Ret operator()(const Args&... args) = 0;

		virtual data_state load_argument_to(arg_type& arguments, bool allow_expired){
			bool any_expired = false;
			const bool success = [&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				return ([&, this]<std::size_t I>(){
					if(!parents[I]) return false;
					node& n = *parents[I];
					using Ty = std::tuple_element_t<I, arg_type>;
					if(auto rst = node_type_cast<Ty>(n).request_raw(allow_expired)){
						if(rst.state() == data_state::expired){
							any_expired = true;
						}
						std::get<I>(arguments) = std::move(rst).value().fetch().value();
						return true;
					}
					return false;
				}.template operator()<Idx>() && ...);
			}(std::index_sequence_for<Args...>{});

			if(!success) return data_state::failed;
			return any_expired ? data_state::expired : data_state::fresh;
		}
	};

	export
	template <typename Ret, typename... Args>
	struct modifier_transient : modifier_base<Ret, Args...>{
	private:
		using base = modifier_base<Ret, Args...>;

	public:
		using base::modifier_base;

		request_pass_handle<Ret> request_raw(bool allow_expired) override{
			typename base::arg_type arguments;
			auto rst = this->load_argument_to(arguments, allow_expired);
			if(rst != data_state::failed){
				if(rst != data_state::expired || allow_expired){
					auto return_value = this->apply(std::move(arguments));
					this->data_pending_state_ = data_pending_state::done;
					return react_flow::make_request_handle_expected(std::move(return_value),
						rst == data_state::expired);
				}
			}

			return make_request_handle_unexpected<Ret>(data_state::failed);
		}

	private:
		bool pull_arguments(typename base::arg_type& arguments, std::size_t target_index,
			push_data_obj* in_data){
			bool any_failed{false};
			[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				([&, this]<std::size_t I>(){
					using Ty = std::tuple_element_t<I, typename base::arg_type>;
					if(I == target_index){
						auto& storage = push_data_cast<Ty>(*in_data);
						std::get<I>(arguments) = storage.get();
					} else{
						if(auto& nptr = static_cast<const raw_node_ptr&>(this->get_inputs()[I])){
							node& n = *nptr;
							if(auto rst = node_type_cast<Ty>(n).request(true)){
								std::get<I>(arguments) = std::move(rst).value();
							} else{
								any_failed = true;
							}
						}
					}
				}.template operator()<Idx>(), ...);
			}(std::index_sequence_for<Args...>{});

			return any_failed;
		}

	protected:
		void on_push(manager& manager, std::size_t target_index, push_data_obj& in_data) override{
			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				typename base::arg_type arguments{};
				const bool any_failed = this->pull_arguments(arguments, target_index, &in_data);

				if(any_failed){
					this->mark_updated(-1);
					return;
				}

				this->data_pending_state_ = data_pending_state::done;
				auto cur = this->apply(std::move(arguments));
				this->base::update_children(manager, cur);
				break;
			}
			case propagate_behavior::lazy :{
				base::mark_updated(-1);
				break;
			}
			case propagate_behavior::pulse :{
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			}
			default : std::unreachable();
			}
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;

			typename base::arg_type arguments{};

			if(this->pull_arguments(arguments, -1, nullptr)){
				this->mark_updated(-1);
				return;
			}

			this->data_pending_state_ = data_pending_state::done;
			auto cur = this->apply(std::move(arguments));
			this->base::update_children(m, cur);
		}
	};

	export
	template <typename Ret, typename... Args>
	struct modifier_argument_cached : modifier_base<Ret, Args...>{
		using base = modifier_base<Ret, Args...>;

	private:
		std::bitset<base::arg_count> dirty{}; //only lazy nodes can be set dirty
		base::arg_type arguments{};

	public:
		using modifier_base<Ret, Args...>::modifier_base;

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(dirty.any()){
				return data_state::expired;
			} else{
				return data_state::fresh;
			}
		}

		request_pass_handle<Ret> request_raw(bool allow_expired) override{
			auto expired = update_arguments();

			if(!expired || allow_expired){
				auto return_value = this->apply(arguments);
				this->data_pending_state_ = data_pending_state::done;
				return react_flow::make_request_handle_expected(std::move(return_value), expired);
			}

			return make_request_handle_unexpected<Ret>(data_state::expired);
		}

	protected:
		void on_push(manager& manager, std::size_t slot, push_data_obj& in_data) override{
			assert(slot < base::arg_count);
			update_data(slot, in_data);

			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				auto cur = this->apply(arguments);
				this->base::update_children(manager, cur);
				break;
			}
			case propagate_behavior::lazy :{
				base::mark_updated(-1);
				break;
			}
			case propagate_behavior::pulse :{
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			}
			default : std::unreachable();
			}
		}

		void mark_updated(std::size_t from) noexcept override{
			dirty.set(from, true);
			base::mark_updated(from);
		}

		data_state load_argument_to(modifier_base<Ret, Args...>::arg_type& arguments, bool allow_expired) final{
			if(auto is_expired = update_arguments()){
				if(allow_expired){
					arguments = this->arguments;
					return data_state::expired;
				}
				return data_state::failed;
			}
			arguments = this->arguments;
			return data_state::fresh;
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;

			this->data_pending_state_ = data_pending_state::done;
			auto cur = this->apply(arguments);
			this->base::update_children(m, cur);
		}

	private:
		bool update_arguments(){
			if(!dirty.any()) return false;
			bool any_expired{};
			[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				([&, this]<std::size_t I>(){
					if(!dirty[I]) return;

					node& n = *static_cast<const raw_node_ptr&>(this->get_inputs()[Idx]);
					if(auto rst = node_type_cast<std::tuple_element_t<Idx, typename base::arg_type>>(n).request(false)){
						dirty.set(I, false);
						std::get<Idx>(arguments) = std::move(rst).value();
					} else{
						any_expired = true;
					}
				}.template operator()<Idx>(), ...);
			}(std::index_sequence_for<Args...>{});
			return any_expired;
		}

		void update_data(std::size_t slot, push_data_obj& in_data){
			dirty.set(slot, false);

			[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				(void)((Idx == slot && (std::get<Idx>(arguments) = push_data_cast<std::tuple_element_t<Idx, typename
					base::arg_type>>(in_data).get(), true)) || ...);
			}(std::index_sequence_for<Args...>{});
		}
	};

	// using O = int;
	// using I = std::string_view;

	export
	template <typename O, typename I>
	struct relay : type_aware_node<O>{
	private:
		static constexpr data_type_index in_type_indices = unstable_type_identity_of<I>();

	protected:
		O cache{};
		raw_node_ptr parent{};
		std::vector<successor_entry> successors{};

	public:
		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{&in_type_indices, 1};
		}

		[[nodiscard]] std::span<const raw_node_ptr> get_inputs() const noexcept final{
			return {&parent, 1};
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors;
		}

		request_pass_handle<O> request_raw(bool allow_expired) final{
			if(this->is_data_expired() && this->parent){
				if(auto rst = react_flow::node_type_cast<I>(*this->parent).request_raw(allow_expired); rst && rst.
					value()){
					this->data_pending_state_ = data_pending_state::done;
					this->cache = this->operator()(std::move(rst).value());
				}
			}

			if(!this->is_data_expired() || allow_expired){
				return react_flow::make_request_handle_expected_ref(this->cache, this->is_data_expired());
			}

			return react_flow::make_request_handle_unexpected<O>(data_state::expired);
		}

	protected:
		void mark_updated(std::size_t from_index) noexcept final{
			type_aware_node<O>::mark_updated(from_index);
		}

		virtual O operator()(const I& input){
			if constexpr(std::convertible_to<const I&, O>){
				return input;
			} else{
				throw invalid_node_error{"not implemented!"};
			}
		}

		virtual O operator()(I&& input){
			return this->operator()(input);
		}

		virtual O operator()(data_package_optimal<I>&& input){
			return this->operator()(input.fetch().value());
		}

		void on_push(manager& manager, std::size_t from_index, push_data_obj& in_data) final{
			assert(from_index == 0);
			auto& storage = push_data_cast<I>(in_data);
			this->cache = this->operator()(storage.get());

			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				this->data_pending_state_ = data_pending_state::done;
				push_data_storage<O> data(this->cache);
				this->push_update(manager, data, this->get_out_socket_type_index());
				break;
			}
			case propagate_behavior::lazy :{
				this->data_pending_state_ = data_pending_state::done;
				std::ranges::for_each(this->get_outputs(), &successor_entry::mark_updated);
				break;
			}
			case propagate_behavior::pulse :{
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			}
			default : break;
			}
		}


		void on_pulse_received(manager& m) final{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;
			push_data_storage<O> data(cache);
			this->push_update(m, data, this->get_out_socket_type_index());
		}

		void disconnect_self_from_context() noexcept final{
			for(const auto& successor : successors){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors.clear();
			if(parent){
				parent->erase_successors_single_edge(0, *this);
				parent = nullptr;
			}
		}

	private:
		bool connect_successors_impl(std::size_t slot, node& post) final{
			if(auto& ptr = post.get_inputs()[slot]){
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return node::try_insert(successors, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return node::try_erase(successors, slot, post);
		}

		void connect_predecessor_impl(std::size_t slot, node& prev) final{
			assert(slot == 0);
			if(parent){
				const auto rng = parent->get_outputs();
				const auto idx = std::ranges::distance(rng.begin(),
					std::ranges::find(rng, this, &successor_entry::get));
				parent->erase_successors_single_edge(idx, *this);
			}
			parent = std::addressof(prev);
		}

		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			assert(slot == 0);
			if(parent == &prev) parent = {};
		}

	public:
		using type_aware_node<O>::type_aware_node;

		[[nodiscard]] data_state get_data_state() const noexcept final{
			return this->is_data_expired() ? data_state::expired : data_state::fresh;
		}

		[[nodiscard]] bool is_isolated() const noexcept final{
			return parent == nullptr && successors.empty();
		}
	};


	template <typename Fn, typename... Args>
	consteval auto test_invoke_result(){
		return std::type_identity<std::invoke_result_t<Fn, Args...>>{};
	}

	//TODO allow optional<T&> one day...

	template <typename Fn, typename... Args>
	using transformer_base_t = modifier_transient<std::decay_t<typename decltype(test_invoke_result<Fn, Args&&
		...>())::type>, Args...>;

	export
	template <typename Fn, typename... Args>
	struct transformer : transformer_base_t<Fn, Args...>{
	private:
		Fn fn;

	public:
		[[nodiscard]] explicit transformer(propagate_behavior data_propagate_type, Fn&& fn)
			: transformer_base_t<Fn, Args...>(data_propagate_type), fn(std::move(fn)){
		}

		[[nodiscard]] explicit transformer(propagate_behavior data_propagate_type, const Fn& fn)
			: transformer_base_t<Fn, Args...>(data_propagate_type), fn(fn){
		}

	protected:
		using ret_t = std::remove_cvref_t<typename decltype(test_invoke_result<Fn, Args&&...>())::type>;

		ret_t operator()(const Args&... args) override{
			return std::invoke(fn, args...);
		}

		ret_t operator()(Args&&... args) override{
			return std::invoke(fn, std::move(args)...);
		}
	};


	template <typename RawFn, typename Tup>
	struct transformer_unambiguous_helper;

	template <typename RawFn, typename... Args>
	struct transformer_unambiguous_helper<RawFn, std::tuple<Args...>>{
		using type = transformer<RawFn, Args...>;
	};

	export
	template <typename Fn>
	using transformer_unambiguous = typename transformer_unambiguous_helper<
		std::remove_cvref_t<Fn>,
		typename function_traits<std::remove_cvref_t<Fn>>::mem_func_args_type
	>::type;

	template <typename T> struct is_async_tuple : std::false_type {};
	template <typename... Args>
	struct is_async_tuple<std::tuple<const async_context&, Args...>> : std::true_type {};

	export
	template <typename Fn>
	auto make_transformer(propagate_behavior data_propagate_type, async_type async_type, Fn&& fn){
		return async_transformer_unambiguous<Fn>{data_propagate_type, async_type, std::forward<Fn>(fn)};
	}

	export
	template <typename Fn>
	auto make_transformer(propagate_behavior data_propagate_type, Fn&& fn){
		return transformer_unambiguous<Fn>{data_propagate_type, std::forward<Fn>(fn)};

	}
}
