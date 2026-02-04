module;

#include <cassert>
#include <stop_token>

export module mo_yanxi.react_flow:async_nodes;

import :manager;
import :node_interface;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{

	template <typename T, typename... Args>
	struct async_node_task;

	export
	template <typename Ret, typename... Args>
	struct async_node_base : type_aware_node<Ret>{
		static_assert((std::is_object_v<Args> && ...) && std::is_object_v<Ret>);

		static constexpr std::size_t arg_count = sizeof...(Args);
		static constexpr std::array<data_type_index, arg_count> in_type_indices{
				unstable_type_identity_of<Args>()...
			};
		using arg_type = std::tuple<std::remove_const_t<Args>...>;

	private:
		std::array<node_ptr, arg_count> parents{};
		std::vector<successor_entry> successors{};

		async_type async_type_{async_type::def};
		trigger_type trigger_type_{trigger_type::active};
		std::size_t dispatched_count_{};
		std::stop_source stop_source_{std::nostopstate};

	public:
		[[nodiscard]] async_node_base() = default;

		[[nodiscard]] explicit async_node_base(async_type async_type)
			: async_type_(async_type){
		}

		[[nodiscard]] explicit async_node_base(propagate_behavior data_propagate_type, async_type async_type)
			: type_aware_node<Ret>(data_propagate_type), async_type_(async_type){
		}

		[[nodiscard]] std::size_t get_dispatched() const noexcept{
			return dispatched_count_;
		}

		[[nodiscard]] async_type get_async_type() const noexcept{
			return async_type_;
		}

		void set_async_type(const async_type async_type) noexcept{
			async_type_ = async_type;
		}

		[[nodiscard]] trigger_type get_trigger_type() const noexcept{
			return trigger_type_;
		}

		void set_trigger_type(const trigger_type trigger_type) noexcept{
			trigger_type_ = trigger_type;
		}

		[[nodiscard]] std::stop_token get_stop_token() const noexcept{
			assert(stop_source_.stop_possible());
			return stop_source_.get_token();
		}

		bool async_cancel() noexcept{
			if(async_type_ == async_type::none) return false;
			if(dispatched_count_ == 0) return false;
			if(!stop_source_.stop_possible()) return false;
			stop_source_.request_stop();
			stop_source_ = std::stop_source{std::nostopstate};
			return true;
		}

		[[nodiscard]] bool is_isolated() const noexcept override{
			return std::ranges::none_of(parents, std::identity{}) && successors.empty();
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			data_state states{};

			for(const node_ptr& p : parents){
				update_state_enum(states, p->get_data_state());
			}

			return states;
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{in_type_indices};
		}

		void disconnect_self_from_context() noexcept final{
			for(std::size_t i = 0; i < parents.size(); ++i){
				if(node_ptr& ptr = parents[i]){
					ptr->erase_successors_single_edge(i, *this);
					ptr = nullptr;
				}
			}
			for(const auto& successor : successors){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors.clear();
		}

		[[nodiscard]] std::span<const node_ptr> get_inputs() const noexcept final{
			return parents;
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors;
		}

	private:
		void async_resume(manager& manager, Ret& data) const{
			this->update_children(manager, data);
		}

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
		void async_launch(manager& manager){
			if(async_type_ == async_type::none){
				throw std::logic_error("async_launch on a synchronized object");
			}

			if(stop_source_.stop_possible()){
				if(async_type_ == async_type::async_latest){
					if(async_cancel()){
						//only reset when really requested stop
						stop_source_ = {};
					}
				}
			} else if(!stop_source_.stop_requested()){
				//stop source empty
				stop_source_ = {};
			}

			++dispatched_count_;

			arg_type arguments{};
			if(auto rst = this->load_argument_to(arguments, true); rst != data_state::failed){
				manager.push_task(std::make_unique<async_node_task<Ret, Args...>>(*this, std::move(arguments)));
			}
		}

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

		std::optional<Ret> apply(const async_context& ctx, arg_type&& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> std::optional<Ret>{
				return this->operator()(ctx, std::move(std::get<Idx>(arguments))...);
			}(std::index_sequence_for<Args...>());
		}

		std::optional<Ret> apply(const async_context& ctx, const arg_type& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> std::optional<Ret>{
				return this->operator()(ctx, std::get<Idx>(arguments)...);
			}(std::index_sequence_for<Args...>());
		}

		virtual std::optional<Ret> operator()(const async_context& ctx, Args&&... args){
			return this->operator()(ctx, std::as_const(args)...);
		}

		virtual std::optional<Ret> operator()(const async_context& ctx, const Args&... args) = 0;

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

		friend async_node_task<Ret, Args...>;
	};

	template <typename T, typename... Args>
	struct async_node_task final : async_task_base{
	private:
		using type = async_node_base<T, Args...>;
		type* modifier_{};
		std::stop_token stop_token_{};

		std::tuple<Args...> arguments_{};
		std::optional<T> rst_cache_{};

	public:
		[[nodiscard]] explicit async_node_task(type& modifier, std::tuple<Args...>&& args) :
			modifier_(std::addressof(modifier)),
			stop_token_(modifier.get_stop_token()), arguments_{std::move(args)}{
		}

		void on_finish(manager& manager) override{
			--modifier_->dispatched_count_;

			if(rst_cache_){
				modifier_->async_resume(manager, rst_cache_.value());
			}
		}

		node* get_owner_if_node() noexcept override{
			return modifier_;
		}

	private:
		void execute() override{
			rst_cache_ = modifier_->apply(async_context{stop_token_, this}, std::move(arguments_));
		}
	};

	export
	template <typename Ret, typename... Args>
	struct async_node_transient : async_node_base<Ret, Args...>{
	private:
		using base = async_node_base<Ret, Args...>;

	public:
		using base::async_node_base;

		request_pass_handle<Ret> request_raw(bool allow_expired) override{
			// Async nodes can't be pulled easily if they are running?
            // "retains old async capability" -> Old modifier_transient checked async_type and dispatched count.
			if(this->get_async_type() != async_type::none){
				if(this->get_dispatched() > 0){
					return make_request_handle_unexpected<Ret>(data_state::awaiting);
				} else{
					return make_request_handle_unexpected<Ret>(data_state::failed);
				}
			}

			// If we are here, async_type is none? But async_node is inherently async capable.
            // But if current task is not running, maybe we can run synchronously?
            // The old code allowed executing sync if async_type == none.
            // But async_node always uses async_context.
            // If the user requests data (pull), can we execute sync?
            // We can construct a dummy async_context with no stop token.

			typename base::arg_type arguments;
			auto rst = this->load_argument_to(arguments, allow_expired);
			if(rst != data_state::failed){
				if(rst != data_state::expired || allow_expired){
                    // Sync execution on pull
					if(auto return_value = this->apply(async_context{}, std::move(arguments))){
						this->data_pending_state_ = data_pending_state::done;
						return react_flow::make_request_handle_expected(std::move(return_value).value(),
							rst == data_state::expired);
					}
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
						if(auto& nptr = static_cast<const node_ptr&>(this->get_inputs()[I])){
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
			if(this->get_trigger_type() == trigger_type::disabled) return;

			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				typename base::arg_type arguments{};
				const bool any_failed = this->pull_arguments(arguments, target_index, &in_data);

				if(any_failed){
					this->mark_updated(-1);
					return;
				}

				this->data_pending_state_ = data_pending_state::done;

				if (this->get_trigger_type() == trigger_type::on_pulse) {
					this->set_trigger_type(trigger_type::disabled);
				}
				if(this->get_async_type() == async_type::none){
					if(auto cur = this->apply(async_context{}, std::move(arguments))){
						this->base::update_children(manager, *cur);
					}
				} else{
					this->async_launch(manager);
				}

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

			if(this->get_trigger_type() == trigger_type::disabled) return;

			if(this->get_trigger_type() == trigger_type::on_pulse){
				this->set_trigger_type(trigger_type::disabled);
			}

			typename base::arg_type arguments{};

			if(this->pull_arguments(arguments, -1, nullptr)){
				this->mark_updated(-1);
				return;
			}

			this->data_pending_state_ = data_pending_state::done;
			if(this->get_async_type() == async_type::none){
				if(auto cur = this->apply(async_context{}, std::move(arguments))){
					this->base::update_children(m, *cur);
				}
			} else{
				this->async_launch(m);
			}
		}
	};

	export
	template <typename Ret, typename... Args>
	struct async_node_cached : async_node_base<Ret, Args...>{
		using base = async_node_base<Ret, Args...>;

	private:
		std::bitset<base::arg_count> dirty{};
		base::arg_type arguments{};

	public:
		using async_node_base<Ret, Args...>::async_node_base;

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(dirty.any()){
				return data_state::expired;
			} else{
				return data_state::fresh;
			}
		}

		request_pass_handle<Ret> request_raw(bool allow_expired) override{
			if(this->get_async_type() != async_type::none){
				if(this->get_dispatched() > 0){
					return make_request_handle_unexpected<Ret>(data_state::awaiting);
				} else{
					return make_request_handle_unexpected<Ret>(data_state::failed);
				}
			}

			auto expired = update_arguments();

			if(!expired || allow_expired){
				if(auto return_value = this->apply(async_context{}, arguments)){
					this->data_pending_state_ = data_pending_state::done;
					return react_flow::make_request_handle_expected(std::move(return_value).value(), expired);
				} else{
					return make_request_handle_unexpected<Ret>(data_state::failed);
				}
			}

			return make_request_handle_unexpected<Ret>(data_state::expired);
		}

	protected:
		void on_push(manager& manager, std::size_t slot, push_data_obj& in_data) override{
			assert(slot < base::arg_count);

			if (this->get_trigger_type() == trigger_type::disabled) {
				mark_updated(slot);
				return;
			}

			update_data(slot, in_data);

			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				if (this->get_trigger_type() == trigger_type::on_pulse) {
					this->set_trigger_type(trigger_type::disabled);
				}
				if(this->get_async_type() == async_type::none){
					if(auto cur = this->apply(async_context{}, arguments)){
						this->base::update_children(manager, *cur);
					}
				} else{
					this->async_launch(manager);
				}
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

		data_state load_argument_to(async_node_base<Ret, Args...>::arg_type& arguments, bool allow_expired) final{
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

			if(this->get_trigger_type() == trigger_type::disabled) return;

			if(this->get_trigger_type() == trigger_type::on_pulse){
				this->set_trigger_type(trigger_type::disabled);
			}

			this->data_pending_state_ = data_pending_state::done;
			if(this->get_async_type() == async_type::none){
				if(auto cur = this->apply(async_context{}, arguments)){
					this->base::update_children(m, *cur);
				}
			} else{
				this->async_launch(m);
			}
		}

	private:
		bool update_arguments(){
			if(!dirty.any()) return false;
			bool any_expired{};
			[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				([&, this]<std::size_t I>(){
					if(!dirty[I]) return;

					node& n = *static_cast<const node_ptr&>(this->get_inputs()[Idx]);
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


	template <typename T>
	struct optional_value_type : std::type_identity<T>{
	};

	template <typename T>
	struct optional_value_type<std::optional<T>> : std::type_identity<T>{
	};

	template <typename T>
	using optional_value_type_t = typename optional_value_type<T>::type;

	template <typename Fn, typename... Args>
	consteval auto test_invoke_result_async(){
		if constexpr(std::invocable<Fn, const async_context&, Args&&...>){
			return std::type_identity<optional_value_type_t<std::invoke_result_t<Fn, const async_context&, Args...>>>
				{};
		} else{
			// Should not happen for async transformer if strict check
			return std::type_identity<optional_value_type_t<std::invoke_result_t<Fn, Args...>>>{};
		}
	}

	template <typename Fn, typename... Args>
	using async_transformer_base_t = async_node_transient<std::decay_t<typename decltype(test_invoke_result_async<Fn, Args&&
		...>())::type>, Args...>;

	export
	template <typename Fn, typename... Args>
	struct async_transformer : async_transformer_base_t<Fn, Args...>{
	private:
		Fn fn;

	public:
		[[nodiscard]] explicit async_transformer(propagate_behavior data_propagate_type, async_type async_type, Fn&& fn)
			: async_transformer_base_t<Fn, Args...>(data_propagate_type, async_type), fn(std::move(fn)){
		}

		[[nodiscard]] explicit async_transformer(propagate_behavior data_propagate_type, async_type async_type, const Fn& fn)
			: async_transformer_base_t<Fn, Args...>(data_propagate_type, async_type), fn(fn){
		}

	protected:
		using ret_t = std::remove_cvref_t<typename decltype(test_invoke_result_async<Fn, Args&&...>())::type>;

		std::optional<ret_t> operator()(
			const async_context& ctx,
			const Args&... args) override{
			if constexpr(std::invocable<Fn, const async_context&, Args&&...>){
				return std::invoke(fn, ctx, args...);
			} else {
				// This branch might be reachable if we allow creating async node with sync function (ignoring context)
				// But explicit instructions implied moving context handling to async_node.
				return std::invoke(fn, args...);
			}
		}

		std::optional<ret_t> operator()(
			const async_context& ctx,
			Args&&... args) override{
			if constexpr(std::invocable<Fn, const async_context&, Args&&...>){
				return std::invoke(fn, ctx, std::move(args)...);
			} else {
				return std::invoke(fn, std::move(args)...);
			}
		}
	};

	template <typename RawFn, typename Tup>
	struct async_transformer_unambiguous_helper;

	template <typename RawFn, typename... Args>
	struct async_transformer_unambiguous_helper<RawFn, std::tuple<Args...>>{
		using type = async_transformer<RawFn, Args...>;
	};

	template <typename RawFn, typename... Args>
	struct async_transformer_unambiguous_helper<RawFn, std::tuple<const async_context&, Args...>>{
		using type = async_transformer<RawFn, Args...>;
	};

	export
	template <typename Fn>
	using async_transformer_unambiguous = typename async_transformer_unambiguous_helper<
		std::remove_cvref_t<Fn>,
		typename function_traits<std::remove_cvref_t<Fn>>::mem_func_args_type
	>::type;

}
