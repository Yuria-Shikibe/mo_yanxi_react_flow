module;

#include <cassert>

export module mo_yanxi.react_flow:nodes;

import :manager;
import :node_interface;

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
				successor.update(*this->manager_, data);
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

	export enum class trigger_type : std::uint8_t{
		disabled,
		on_pulse,
		active
	};

	template <typename T, typename... Args>
	struct async_node_task;

	export
	struct async_context{
		void* task;
		std::stop_token stop_token;

		[[nodiscard]] std::stop_token get_stop_token() const noexcept{
			return stop_token;
		}
	};

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

		async_type async_type_{};
		std::size_t dispatched_count_{};
		std::stop_source stop_source_{std::nostopstate};
		trigger_type trigger_type_{trigger_type::active};

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

		void set_trigger_type(trigger_type t) noexcept{
			trigger_type_ = t;
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

		std::optional<Ret> apply(async_node_task<Ret, Args...>* task, arg_type&& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> std::optional<Ret>{
				return this->operator()(async_context{task, task->get_token()}, std::move(std::get<Idx>(arguments))...);
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

		[[nodiscard]] std::stop_token get_token() const noexcept{
			return stop_token_;
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
			rst_cache_ = modifier_->apply(this, std::move(arguments_));
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
			// Async nodes generally do not support synchronous pull if they rely on async execution
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
				this->async_launch(manager);

				if(this->get_trigger_type() == trigger_type::on_pulse){
					this->set_trigger_type(trigger_type::disabled);
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

			typename base::arg_type arguments{};

			if(this->pull_arguments(arguments, -1, nullptr)){
				this->mark_updated(-1);
				return;
			}

			this->data_pending_state_ = data_pending_state::done;
			this->async_launch(m);

			if(this->get_trigger_type() == trigger_type::on_pulse){
				this->set_trigger_type(trigger_type::disabled);
			}
		}
	};

	export
	template <typename Ret, typename... Args>
	struct async_node_argument_cached : async_node_base<Ret, Args...>{
		using base = async_node_base<Ret, Args...>;

	private:
		std::bitset<base::arg_count> dirty{};
		typename base::arg_type arguments{};

	public:
		using base::async_node_base;

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(dirty.any()){
				return data_state::expired;
			} else{
				return data_state::fresh;
			}
		}

		request_pass_handle<Ret> request_raw(bool allow_expired) override{
			return make_request_handle_unexpected<Ret>(data_state::failed);
		}

	protected:
		void on_push(manager& manager, std::size_t slot, push_data_obj& in_data) override{
			if(this->get_trigger_type() == trigger_type::disabled) return;

			assert(slot < base::arg_count);
			update_data(slot, in_data);

			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				this->async_launch(manager);
				if(this->get_trigger_type() == trigger_type::on_pulse){
					this->set_trigger_type(trigger_type::disabled);
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

		data_state load_argument_to(typename base::arg_type& arguments, bool allow_expired) final{
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

			this->data_pending_state_ = data_pending_state::done;
			this->async_launch(m);

			if(this->get_trigger_type() == trigger_type::on_pulse){
				this->set_trigger_type(trigger_type::disabled);
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

	template <typename Ret, typename... Args>
	struct modifier_base : type_aware_node<Ret>{
		static_assert((std::is_object_v<Args> && ...) && std::is_object_v<Ret>);

		static constexpr std::size_t arg_count = sizeof...(Args);
		static constexpr std::array<data_type_index, arg_count> in_type_indices{
				unstable_type_identity_of<Args>()...
			};
		using arg_type = std::tuple<std::remove_const_t<Args>...>;

	private:
		std::array<node_ptr, arg_count> parents{};
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

		std::optional<Ret> apply(arg_type&& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> std::optional<Ret>{
				return this->operator()(std::move(std::get<Idx>(arguments))...);
			}(std::index_sequence_for<Args...>());
		}

		std::optional<Ret> apply(const arg_type& arguments){
			return [&, this]<std::size_t... Idx>(std::index_sequence<Idx...>) -> std::optional<Ret>{
				return this->operator()(std::get<Idx>(arguments)...);
			}(std::index_sequence_for<Args...>());
		}

		virtual std::optional<Ret> operator()(Args&&... args){
			return this->operator()(std::as_const(args)...);
		}

		virtual std::optional<Ret> operator()(const Args&... args) = 0;

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
					if(auto return_value = this->apply(std::move(arguments))){
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
			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				typename base::arg_type arguments{};
				const bool any_failed = this->pull_arguments(arguments, target_index, &in_data);

				if(any_failed){
					this->mark_updated(-1);
					return;
				}

				this->data_pending_state_ = data_pending_state::done;
				if(auto cur = this->apply(std::move(arguments))){
					this->base::update_children(manager, *cur);
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

			typename base::arg_type arguments{};

			if(this->pull_arguments(arguments, -1, nullptr)){
				this->mark_updated(-1);
				return;
			}

			this->data_pending_state_ = data_pending_state::done;
			if(auto cur = this->apply(std::move(arguments))){
				this->base::update_children(m, *cur);
			}
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
				if(auto return_value = this->apply(arguments)){
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
			update_data(slot, in_data);

			switch(this->get_propagate_type()){
			case propagate_behavior::eager :{
				if(auto cur = this->apply(arguments)){
					this->base::update_children(manager, *cur);
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
			if(auto cur = this->apply(arguments)){
				this->base::update_children(m, *cur);
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

	// using O = int;
	// using I = std::string_view;

	export
	template <typename O, typename I>
	struct relay : type_aware_node<O>{
	private:
		static constexpr data_type_index in_type_indices = unstable_type_identity_of<I>();

	protected:
		O cache{};
		node_ptr parent{};
		std::vector<successor_entry> successors{};

	public:
		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{&in_type_indices, 1};
		}

		[[nodiscard]] std::span<const node_ptr> get_inputs() const noexcept final{
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
				throw invalid_node{"not implemented!"};
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


	template <typename T>
	struct optional_value_type : std::type_identity<T>{
	};

	template <typename T>
	struct optional_value_type<std::optional<T>> : std::type_identity<T>{
	};

	template <typename T>
	using optional_value_type_t = typename optional_value_type<T>::type;

	template <typename Fn, typename... Args>
	consteval auto test_invoke_result(){
		return std::type_identity<optional_value_type_t<std::invoke_result_t<Fn, Args...>>>{};
	}

	//TODO allow optional<T&> one day...

	template <typename Fn, typename... Args>
	using transformer_ret_t = std::decay_t<typename decltype(test_invoke_result<Fn, Args&&...>()) ::type>;

	template <typename... Args>
	struct analyze_transformer_args;

	template <typename First, typename... Rest>
	struct analyze_transformer_args<First, Rest...>{
		static constexpr bool has_context = std::is_same_v<std::decay_t<First>, async_context>;
		using node_args = std::conditional_t<has_context, std::tuple<Rest...>, std::tuple<First, Rest...>>;
	};

	template <>
	struct analyze_transformer_args<>{
		static constexpr bool has_context = false;
		using node_args = std::tuple<>;
	};

	template <typename Fn, typename... Args>
	constexpr bool is_async_transformer_v = analyze_transformer_args<Args...>::has_context;

	template <template <typename, typename...> class NodeT, typename Ret, typename Tuple>
	struct instantiate_node_from_tuple;

	template <template <typename, typename...> class NodeT, typename Ret, typename... NodeArgs>
	struct instantiate_node_from_tuple<NodeT, Ret, std::tuple<NodeArgs...>>{
		using type = NodeT<Ret, NodeArgs...>;
	};

	template <typename Fn, typename Ret, typename Tuple>
	struct transformer_async_invoker;

	template <typename Fn, typename Ret, typename... NodeArgs>
	struct transformer_async_invoker<Fn, Ret, std::tuple<NodeArgs...>> : async_node_transient<Ret, NodeArgs...>{
		using base = async_node_transient<Ret, NodeArgs...>;
		Fn fn;

		using base::base;

		explicit transformer_async_invoker(propagate_behavior p, async_type a, Fn&& fn)
			: base(p, a), fn(std::move(fn)){
		}

		explicit transformer_async_invoker(propagate_behavior p, async_type a, const Fn& fn)
			: base(p, a), fn(fn){
		}

	protected:
		std::optional<Ret> operator()(const async_context& ctx, const NodeArgs&... args) override{
			return std::invoke(fn, ctx, args...);
		}

		std::optional<Ret> operator()(const async_context& ctx, NodeArgs&&... args) override{
			return std::invoke(fn, ctx, std::move(args)...);
		}
	};

	template <bool IsAsync, typename Fn, typename Ret, typename... Args>
	struct transformer_impl;

	// Sync Impl
	template <typename Fn, typename Ret, typename... Args>
	struct transformer_impl<false, Fn, Ret, Args...> : modifier_transient<Ret, Args...>{
		using base = modifier_transient<Ret, Args...>;
		Fn fn;

		explicit transformer_impl(propagate_behavior p, async_type, Fn&& fn)
			: base(p), fn(std::move(fn)){
		}

		explicit transformer_impl(propagate_behavior p, async_type, const Fn& fn)
			: base(p), fn(fn){
		}

	protected:
		std::optional<Ret> operator()(const Args&... args) override{
			return std::invoke(fn, args...);
		}

		std::optional<Ret> operator()(Args&&... args) override{
			return std::invoke(fn, std::move(args)...);
		}
	};

	// Async Impl
	template <typename Fn, typename Ret, typename... Args>
	struct transformer_impl<true, Fn, Ret, Args...>
		: transformer_async_invoker<Fn, Ret, typename analyze_transformer_args<Args...>::node_args>{
		using base = transformer_async_invoker<Fn, Ret, typename analyze_transformer_args<Args...>::node_args>;
		using base::base;
	};

	export
	template <typename Fn, typename... Args>
	struct transformer : transformer_impl<is_async_transformer_v<Fn, Args...>, Fn, transformer_ret_t<Fn, Args...>,
			Args...>{
		using base = transformer_impl<is_async_transformer_v<Fn, Args...>, Fn, transformer_ret_t<Fn, Args...>, Args...>;
		using base::base;
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

	export
	template <typename Fn>
	transformer_unambiguous<Fn> make_transformer(propagate_behavior data_propagate_type, async_type async_type, Fn&& fn){
		return transformer_unambiguous<Fn>{data_propagate_type, async_type, std::forward<Fn>(fn)};
	}
}
