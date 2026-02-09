module;

#include <cassert>
#include <stop_token>

export module mo_yanxi.react_flow:async;

import :manager;
import :node_interface;
import mo_yanxi.type_register;
import mo_yanxi.meta_programming;
import std;

namespace mo_yanxi::react_flow{

	template <typename T, typename... Args>
	struct async_node_task;

	export struct async_context{
		std::stop_token node_stop_token{};
		std::stop_token manager_stop_token{};
		progressed_async_node_base* task{};

		//TODO add manager stop token?

		bool stop_requested() const noexcept{
			return node_stop_token.stop_requested() || manager_stop_token.stop_requested();
		}
	};

	export
	template <typename Ret, typename... Args>
	struct async_node : type_aware_node<Ret>{
		static_assert((std::is_object_v<Args> && ...) && std::is_object_v<Ret>);

		static constexpr std::size_t arg_count = sizeof...(Args);
		static constexpr std::array<data_type_index, arg_count> in_type_indices{
				unstable_type_identity_of<Args>()...
			};
		using arg_type = std::tuple<std::remove_const_t<Args>...>;

	private:
		std::array<raw_node_ptr, arg_count> parents_{};
		std::vector<successor_entry> successors_{};

		manager* manager_{};

		//TODO successor_that_receives progress
		node_holder<provider_general<progress_check>> progress_provider_{};

		async_type async_type_{async_type::def};
		trigger_type trigger_type_{trigger_type::active};
		std::size_t dispatched_count_{};
		std::stop_source stop_source_{std::nostopstate};

		std::bitset<arg_count> dirty_{};
		arg_type arguments_{};



		/**
		 * @brief if true, the params is moved instead of copy on a task launch
		 */
		bool move_params_on_launch_{false};

	public:
		[[nodiscard]] async_node() = default;

		[[nodiscard]] explicit async_node(async_type async_type)
			: async_type_(async_type){
		}

		[[nodiscard]] explicit async_node(propagate_type data_propagate_type, async_type async_type)
			: type_aware_node<Ret>(data_propagate_type), async_type_(async_type){
		}

		[[nodiscard]] explicit async_node(manager& manager, async_type async_type)
			: manager_(&manager), async_type_(async_type){
		}

		[[nodiscard]] explicit async_node(manager& manager, propagate_type data_propagate_type, async_type async_type)
			: type_aware_node<Ret>(data_propagate_type), manager_(&manager), async_type_(async_type){
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

		bool add_progress_receiver(node& node){
			return progress_provider_->connect_successor(node);
		}

		bool erase_progress_receiver(node& node){
			return progress_provider_->disconnect_successor(node);
		}

		[[nodiscard]] std::stop_token get_stop_token() const noexcept{
			assert(stop_source_.stop_possible());
			return stop_source_.get_token();
		}

		bool async_cancel() noexcept{
			if(dispatched_count_ == 0) return false;
			if(!stop_source_.stop_possible()) return false;
			stop_source_.request_stop();
			stop_source_ = std::stop_source{std::nostopstate};
			return true;
		}

		[[nodiscard]] bool is_isolated() const noexcept override{
			return std::ranges::none_of(parents_, std::identity{}) && successors_.empty();
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(dirty_.any()){
				return data_state::expired;
			}

			return get_dispatched() ? data_state::awaiting : data_state::failed;
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{in_type_indices};
		}

		void set_manager(manager& manager) override{
			manager_ = &manager;
		}

		void disconnect_self_from_context() noexcept final{
			for(std::size_t i = 0; i < parents_.size(); ++i){
				if(raw_node_ptr& ptr = parents_[i]){
					ptr->erase_successors_single_edge(i, *this);
					ptr = nullptr;
				}
			}
			for(const auto& successor : successors_){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors_.clear();
		}

		[[nodiscard]] std::span<const raw_node_ptr> get_inputs() const noexcept final{
			return parents_;
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors_;
		}

		request_pass_handle<Ret> request_raw(bool allow_expired) final{
			if(this->get_dispatched() > 0){
				return make_request_handle_unexpected<Ret>(data_state::awaiting);
			}
			return make_request_handle_unexpected<Ret>(data_state::failed);
		}



	protected:

		void on_push(std::size_t slot, data_carrier_obj&& in_data) override{
			assert(slot < arg_count);

			update_data(slot, in_data);

			auto trigger = this->get_trigger_type();
			if (trigger == trigger_type::disabled) {
				return;
			}
			if (trigger == trigger_type::on_pulse) {
				this->set_trigger_type(trigger_type::disabled);
			}

			switch (this->get_propagate_type()) {
			case propagate_type::eager:
				this->async_launch();
				break;
			case propagate_type::lazy:
				mark_updated(-1);
				break;
			case propagate_type::pulse:
				this->data_pending_state_ = data_pending_state::waiting_pulse;
				break;
			default:
				std::unreachable();
			}
		}

		void mark_updated(std::size_t from) noexcept override{
			//TODO discard update when from is updating trigger type?
			if(from != -1){
				dirty_.set(from, true);
			}
			type_aware_node<Ret>::mark_updated(from);
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;
			this->async_launch();
		}

		void async_launch(){
			assert(manager_);
			if(stop_source_.stop_possible()){
				if(async_type_ == async_type::async_latest){
					if(async_cancel()){
						stop_source_ = {};
					}
				}
			} else if(!stop_source_.stop_requested()){
				stop_source_ = {};
			}

			++dispatched_count_;

			arg_type args_copy{};
			// 使用合并后的 load_argument_to 逻辑
			if(auto rst = this->load_argument_to(args_copy, true); rst != data_state::failed){
				manager_->push_task(std::make_unique<async_node_task<Ret, Args...>>(*this, std::move(args_copy)));
			}
		}

		data_state load_argument_to(arg_type& out_args, bool allow_expired){
			if(auto is_expired = update_arguments()){
				if(allow_expired){
					if (move_params_on_launch_){
						out_args = std::move(this->arguments_);
					}else{
						out_args = this->arguments_;
					}

					return data_state::expired;
				}
				return data_state::failed;
			}

			if (move_params_on_launch_){
				out_args = std::move(this->arguments_);
			}else{
				out_args = this->arguments_;
			}

			return data_state::fresh;
		}

		Ret apply(const async_context& ctx, arg_type&& args){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>){
				return this->operator()(ctx, std::move(std::get<Idx>(args))...);
			}(std::index_sequence_for<Args...>());
		}

		virtual Ret operator()(const async_context& ctx, Args&&... args) = 0;

	private:
		// --- 内部连接辅助 ---

		void async_done(Ret&& val) const{
			react_flow::push_to_successors(successors_, data_carrier{std::move(val)});
		}

		bool connect_successors_impl(std::size_t slot, node& post) final{
			if(auto& ptr = post.get_inputs()[slot]){
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return node::try_insert(successors_, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return node::try_erase(successors_, slot, post);
		}

		void connect_predecessor_impl(std::size_t slot, node& prev) final{
			if(parents_[slot]){
				const auto rng = parents_[slot]->get_outputs();
				const auto idx = std::ranges::distance(rng.begin(),
					std::ranges::find(rng, this, &successor_entry::get));
				parents_[slot]->erase_successors_single_edge(idx, *this);
			}
			parents_[slot] = std::addressof(prev);
		}

		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			if(parents_[slot] == &prev){
				parents_[slot] = {};
			}
		}

		// --- 缓存更新辅助 ---

		bool update_arguments(){
			if(!dirty_.any()) return false;

			bool any_expired{};
			[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				([&, this]<std::size_t I>(){
					if(!dirty_[I]) return;
					if(!parents_[I]) return;

					node& n = *parents_[I];
					if(auto rst = node_type_cast<std::tuple_element_t<I, arg_type>>(n).request(false)){
						dirty_.set(I, false);
						std::get<I>(arguments_) = std::move(rst).value();
						using type = std::tuple_element_t<I, arg_type>;
						if constexpr(std::same_as<std::decay_t<type>, trigger_type>){
							this->set_trigger_type(std::get<I>(arguments_));
						}
					} else{
						any_expired = true;
					}
				}.template operator()<Idx>(), ...);
			}(std::index_sequence_for<Args...>{});
			return any_expired;
		}

		void update_data(std::size_t slot, data_carrier_obj& in_data){
			dirty_.set(slot, false);

			[&, this]<std::size_t ... Idx>(std::index_sequence<Idx...>){
				([&, this]<std::size_t I>(){
					if(I == slot){
						using type = std::tuple_element_t<I, arg_type>;
						std::get<I>(arguments_) = push_data_cast<type>(in_data).get();
						if constexpr(std::same_as<std::decay_t<type>, trigger_type>){
							this->set_trigger_type(std::get<I>(arguments_));
						}
						return true;
					}
					return false;
				}.template operator()<Idx>() || ...);
			}(std::index_sequence_for<Args...>{});
		}

		friend async_node_task<Ret, Args...>;
	};

	template <typename T, typename... Args>
	struct async_node_task final : progressed_async_node_base{
	private:
		using type = async_node<T, Args...>;
		node_pointer modifier_{};
		std::stop_token stop_token_;

		std::tuple<Args...> arguments_{};
		T rst_cache_{};

		provider_general<progress_check>* prov{};

	private:
		type& get() const noexcept{
			return static_cast<type&>(*modifier_);
		}
	public:
		[[nodiscard]] explicit async_node_task(type& modifier, std::tuple<Args...>&& args) :
			progressed_async_node_base{!modifier.progress_provider_->get_outputs().empty()},
			modifier_(std::addressof(modifier)), stop_token_(modifier.get_stop_token()),
			arguments_{std::move(args)}, prov(std::addressof(*modifier.progress_provider_)){
		}

		void on_finish(manager& manager) override{
			--get().dispatched_count_;
			set_progress_done();

			get().async_done(std::move(rst_cache_));
		}

		node* get_owner_if_node() noexcept override{
			return modifier_.get();
		}

		void on_update_check(manager& manager) override{
			if(const auto prog = get_progress(); prog.changed){
				prov->update_value(prog);
			}
		}

	private:
		void execute(const manager& manager) override{
			rst_cache_ = get().apply(async_context{stop_token_, manager.get_manager_stop_token(), this}, std::move(arguments_));
		}
	};


	template <typename T>
	using optional_value_type_t = typename std::optional<T>::value_type;

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
	using async_transformer_base_t = async_node<std::decay_t<typename decltype(test_invoke_result_async<Fn, Args&&...>()
	)::type>, Args...>;

	export
	template <typename Fn, typename... Args>
	struct async_transformer : async_transformer_base_t<Fn, Args...>{
	private:
		Fn fn;

	public:
		[[nodiscard]] explicit async_transformer(propagate_type data_propagate_type, async_type async_type, Fn&& fn)
			: async_transformer_base_t<Fn, Args...>(data_propagate_type, async_type), fn(std::move(fn)){
		}

		[[nodiscard]] explicit async_transformer(propagate_type data_propagate_type, async_type async_type,
			const Fn& fn)
			: async_transformer_base_t<Fn, Args...>(data_propagate_type, async_type), fn(fn){
		}

		[[nodiscard]] explicit async_transformer(propagate_type data_propagate_type, async_type async_type, Fn&& fn, manager& manager)
			: async_transformer_base_t<Fn, Args...>(data_propagate_type, async_type, manager), fn(std::move(fn)){
		}

		[[nodiscard]] explicit async_transformer(propagate_type data_propagate_type, async_type async_type,
			const Fn& fn, manager& manager)
			: async_transformer_base_t<Fn, Args...>(data_propagate_type, async_type, manager), fn(fn){
		}

		[[nodiscard]] async_transformer(const async_transformer& other, manager& manager)
			: async_transformer_base_t<Fn, Args...>(other, manager), fn(other.fn){
		}

		[[nodiscard]] async_transformer(async_transformer&& other, manager& manager)
			: async_transformer_base_t<Fn, Args...>(std::move(other), manager), fn(std::move(other.fn)){
		}

	protected:
		using ret_t = std::remove_cvref_t<typename decltype(test_invoke_result_async<Fn, Args&&...>())::type>;

		ret_t operator()(
			const async_context& ctx,
			Args&&... args) override{
			if constexpr(std::invocable<Fn, const async_context&, Args&&...>){
				return std::invoke(fn, ctx, std::move(args)...);
			} else{
				return std::invoke(fn, std::move(args)...);
			}
		}
	};

	template <typename RawFn, typename Tup>
	struct async_transformer_unambiguous_helper;

	template <typename RawFn, typename... Args>
	struct async_transformer_unambiguous_helper<RawFn, std::tuple<Args...>>{
		using type = async_transformer<RawFn, std::decay_t<Args>...>;
	};

	template <typename RawFn, typename... Args>
	struct async_transformer_unambiguous_helper<RawFn, std::tuple<const async_context&, Args...>>{
		using type = async_transformer<RawFn, std::decay_t<Args>...>;
	};

	export
	template <typename Fn>
	using async_transformer_unambiguous = typename async_transformer_unambiguous_helper<
		std::remove_cvref_t<Fn>,
		typename function_traits<std::remove_cvref_t<Fn>>::mem_func_args_type
	>::type;
}
