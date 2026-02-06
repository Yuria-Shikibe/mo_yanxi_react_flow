module;

#include <cassert>

#ifndef MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
#define MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK 1
#endif

#ifndef MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK
#define MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK 1
#endif

#ifndef MO_YANXI_DATA_FLOW_DISABLE_THREAD_CHECK
#define THREAD_CHECK
#endif


export module mo_yanxi.react_flow:node_interface;
import std;

import mo_yanxi.type_register;
export import mo_yanxi.react_flow.data_storage;

namespace mo_yanxi::react_flow{
	using data_type_index = type_identity_index;

	export struct manager;

	export struct node;

	export
	struct node_pointer{
	private:
		node* node_{};
	public:
		inline constexpr node_pointer() = default;

		inline explicit(false) node_pointer(node* node)
			: node_(node){
			if(node_)incr_();
		}

		inline explicit(false) node_pointer(node& node)
			: node_(&node){
			incr_();
		}

		template <typename T, typename ...Args>
			requires std::constructible_from<T, Args&&...>
		inline explicit node_pointer(std::in_place_type_t<T>, Args&& ...args)
			: node_(new T(std::forward<Args>(args)...)){
			incr_();
		}

		inline ~node_pointer(){
			if(node_)decr_();
		}

		inline explicit operator bool() const noexcept{
			return node_ != nullptr;
		}

		inline node* get() const noexcept{
			return node_;
		}

		inline void reset(node* p = nullptr){
			if(node_)decr_();
			node_ = p;
			if(node_)incr_();
		}

		inline node_pointer(const node_pointer& other) noexcept
			: node_(other.node_){
			if(node_)incr_();
		}

		inline node_pointer(node_pointer&& other) noexcept
			: node_(std::exchange(other.node_, nullptr)){
		}

		inline node_pointer& operator=(const node_pointer& other) noexcept{
			if(this == &other) return *this;
			reset(other.node_);
			return *this;
		}

		inline node_pointer& operator=(node_pointer&& other) noexcept{
			if(this == &other) return *this;
			if(node_)decr_();
			node_ = std::exchange(other.node_, nullptr);
			return *this;
		}

		inline bool operator==(const node_pointer&) const noexcept = default;

		friend inline bool operator==(const node_pointer& lhs, const node* rhs) noexcept{
			return lhs.node_ == rhs;
		}

		friend inline bool operator==(const node* lhs, const node_pointer& rhs) noexcept{
			return lhs == rhs.node_;
		}

		inline node& operator*() const noexcept{
			assert(node_ != nullptr);
			return *node_;
		}

		inline node* operator->() const noexcept{
			return node_;
		}

	private:
		inline void incr_() const noexcept;

		inline void decr_() const noexcept;
	};

	export
	struct invalid_node_error : std::invalid_argument{
		[[nodiscard]] explicit invalid_node_error(const std::string& msg)
			: invalid_argument(msg){
		}

		[[nodiscard]] explicit invalid_node_error(const char* msg)
			: invalid_argument(msg){
		}
	};


	export
	template <typename T>
	struct terminal;

	template <typename Ret, typename... Args>
	struct modifier_base;

	export
	template <typename T>
	struct intermediate_cache;

	export
	using raw_node_ptr = node*;

	export
	struct successor_entry{
		std::size_t index;
		node_pointer entity;

		[[nodiscard]] successor_entry() = default;

		[[nodiscard]] successor_entry(const std::size_t index, node& entity)
			: index(index),
			entity(entity){
		}

		[[nodiscard]] node* get() const noexcept{
			return entity.get();
		}

		template <typename T>
		void update(push_data_storage<T>& data) const;

		void update(push_data_obj& data, data_type_index checker) const;

		void mark_updated() const;
	};


	/**
	 * @brief 判断在 n0 的 output 中加入 n1 后是否会形成环
	 *
	 * @param self 边的起始节点
	 * @param successors 边的目标节点
	 * @return 如果会形成环，返回 true, 否则返回 false
	 */
	export
	bool is_ring_bridge(const node* self, const node* successors);

	struct node{
		friend successor_entry;
		friend manager;
		friend node_pointer;

		template <typename Ret, typename... Args>
		friend struct modifier_base;

		template <typename T>
		friend struct intermediate_cache;

	private:
		std::size_t reference_count_{};

#ifdef THREAD_CHECK
		std::thread::id created_thread_id_{std::this_thread::get_id()};
#endif


	protected:
		propagate_behavior data_propagate_type_{};
		data_pending_state data_pending_state_{};

	public:
		[[nodiscard]] node() = default;

		[[nodiscard]] explicit node(propagate_behavior data_propagate_type)
			: data_propagate_type_(data_propagate_type){
		}

		virtual ~node() = default;

		void incr_ref() noexcept{
#ifdef THREAD_CHECK
			if(created_thread_id_ != std::this_thread::get_id()){
				std::println(std::cerr, "operate reference count on a wrong thread");
				std::terminate();
			}
#endif

			++reference_count_;
		}

		bool decr_ref() noexcept{
#ifdef THREAD_CHECK
			if(created_thread_id_ != std::this_thread::get_id()){
				std::println(std::cerr, "operate reference count on a wrong thread");
				std::terminate();
			}
#endif

			--reference_count_;
			return reference_count_ == 0;
		}

		bool is_droppable() const noexcept{
#ifdef THREAD_CHECK
			if(created_thread_id_ != std::this_thread::get_id()){
				std::println(std::cerr, "operate reference count on a wrong thread");
				std::terminate();
			}
#endif

			return reference_count_ == 0;
		}

		[[nodiscard]] propagate_behavior get_propagate_type() const noexcept{
			return data_propagate_type_;
		}

		[[nodiscard]] bool is_data_expired() const noexcept{
			return data_pending_state_ != data_pending_state::done;
		}

#pragma region ConncectionInterface

	public:
		[[nodiscard]] virtual data_type_index get_out_socket_type_index() const noexcept{
			return nullptr;
		}

		[[nodiscard]] virtual std::span<const data_type_index> get_in_socket_type_index() const noexcept{
			return {};
		}

		[[nodiscard]] virtual data_state get_data_state() const noexcept{
			return data_state::failed;
		}

		[[nodiscard]] virtual std::span<const raw_node_ptr> get_inputs() const noexcept{
			return {};
		}

		[[nodiscard]] virtual std::span<const successor_entry> get_outputs() const noexcept{
			return {};
		}

		bool connect_successors_unchecked(const std::size_t slot_of_successor, node& post){
			if(connect_successors_impl(slot_of_successor, post)){
				post.connect_predecessor_impl(slot_of_successor, *this);
				return true;
			}
			return false;
		}

		bool connect_predecessor_unchecked(const std::size_t slot_of_successor, node& prev){
			return prev.connect_successors_unchecked(slot_of_successor, *this);
		}

		bool connect_successors(const std::size_t slot, node& post){
#if MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK
			if(is_ring_bridge(this, &post)){
				throw invalid_node_error{"ring detected"};
			}
#endif


			if(!std::ranges::contains(post.get_in_socket_type_index(), get_out_socket_type_index())){
				throw invalid_node_error{"Node type NOT match"};
			}

			return connect_successors_unchecked(slot, post);
		}

		bool connect_predecessor(const std::size_t slot, node& prev){
			return prev.connect_successors(slot, *this);
		}

		bool connect_successors(node& post){
#if MO_YANXI_DATA_FLOW_ENABLE_RING_CHECK
			if(is_ring_bridge(this, &post)){
				throw invalid_node_error{"ring detected"};
			}
#endif

			const auto rng = post.get_in_socket_type_index();
			if(auto itr = std::ranges::find(rng, get_out_socket_type_index()); itr != rng.end()){
				return connect_successors_unchecked(std::ranges::distance(rng.begin(), itr), post);
			} else{
				throw invalid_node_error{"Failed To Find Slot"};
			}
		}

		bool connect_predecessor(node& prev){
			return prev.connect_successors(*this);
		}

		bool disconnect_successors(const std::size_t slot, node& post) noexcept{
			if(erase_successors_single_edge(slot, post)){
				post.erase_predecessor_single_edge(slot, *this);
				return true;
			}
			return false;
		}

		bool disconnect_predecessor(const std::size_t slot, node& prev) noexcept{
			return prev.disconnect_successors(slot, *this);
		}


		bool disconnect_successors(node& post){
			const auto rng = post.get_in_socket_type_index();
			if(auto itr = std::ranges::find(rng, get_out_socket_type_index()); itr != rng.end()){
				auto slot = std::ranges::distance(rng.begin(), itr);
				if(erase_successors_single_edge(slot, post)){
					post.erase_predecessor_single_edge(slot, *this);
					return true;
				}
			} else{
				throw invalid_node_error{"Failed To Find Slot"};
			}

			return false;
		}

		bool disconnect_predecessor(node& prev){
			return prev.disconnect_successors(*this);
		}

		virtual void disconnect_self_from_context() noexcept{
		}

		virtual void set_manager(manager& manager){
		}

		[[nodiscard]] virtual bool is_isolated() const noexcept{
			return false;
		}

	public:
		virtual bool erase_successors_single_edge(std::size_t slot, node& post) noexcept{
			return false;
		}

		virtual void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept{
		}

	public:
		//TODO disconnected conflicted
		virtual bool connect_successors_impl(std::size_t slot, node& post){
			return false;
		}

		virtual void connect_predecessor_impl(std::size_t slot, node& prev){
		}

#pragma endregion

	protected:
		void push_update(this const auto& self, push_data_obj& data, data_type_index idx){
			if(self.data_propagate_type_ == propagate_behavior::eager){
				for(const successor_entry& successor : self.get_outputs()){
					successor.update(data, idx);
				}
			} else{
				std::ranges::for_each(self.get_outputs(), &successor_entry::mark_updated);
			}
		}

		virtual void on_pulse_received(manager& m){
			return;
		}

		/**
		 * @brief update the node
		 *
		 * The data flow is from source to terminal (push)
		 *
		 * @param in_data ptr to const data, provided by parent
		 */
		virtual void on_push(std::size_t from_index, push_data_obj& in_data){
		}

		/**
		 *
		 * @brief Only notify the data has changed, used for lazy nodes. Should be mutually exclusive with update when propagate.
		 *
		 */
		virtual void mark_updated(std::size_t from_index) noexcept{
			data_pending_state_ = data_pending_state::expired;
			std::ranges::for_each(get_outputs(), &successor_entry::mark_updated);
		}


		static bool try_insert(std::vector<successor_entry>& successors, std::size_t slot, node& next){
			if(std::ranges::find_if(successors, [&](const successor_entry& e){
				return e.index == slot && e.entity == &next;
			}) != successors.end()){
				return false;
			}

			successors.emplace_back(slot, next);
			return true;
		}

		static bool try_erase(std::vector<successor_entry>& successors, const std::size_t slot,
			const node& next) noexcept{
			return std::erase_if(successors, [&](const successor_entry& e){
				return e.index == slot && e.entity == &next;
			});
		}
	};

	export
	template <typename T>
	struct type_aware_node : node{
		using node::node;

		[[nodiscard]] data_type_index get_out_socket_type_index() const noexcept final{
			return unstable_type_identity_of<T>();
		}

		template <typename S>
		std::optional<T> request(this S& self, bool allow_expired){
			if(auto rst = self.request_raw(allow_expired)){
				return rst.value().fetch();
			} else{
				return std::nullopt;
			}
		}

		template <typename S>
		std::expected<T, data_state> request_stated(this S& self, bool allow_expired){
			if(auto rst = self.request_raw(allow_expired)){
				if(auto optal = rst.value().fetch()){
					return std::expected<T, data_state>{optal.value()};
				}
				return std::unexpected{data_state::failed};
			} else{
				return std::unexpected{rst.error()};
			}
		}

		template <typename S>
		std::optional<T> nothrow_request(this S& self, bool allow_expired) noexcept try{
			if(auto rst = self.request_raw(allow_expired)){
				return rst.value().fetch();
			}
			return std::nullopt;
		} catch(...){
			return std::nullopt;
		}

		template <typename S>
		std::expected<T, data_state> nothrow_request_stated(this S& self, bool allow_expired) noexcept try{
			if(auto rst = self.request_raw(allow_expired)){
				if(auto optal = rst.value().fetch()){
					return std::expected<T, data_state>{optal.value()};
				}
			}
			return std::unexpected{data_state::failed};
		} catch(...){
			return std::unexpected{data_state::failed};
		}

		/**
		 * @brief Pull data from parent node
		 *
		 * The data flow is from terminal to source (pull)
		 *
		 * This member function is not const as it may update the internal cache
		 *
		 * @param allow_expired
		 */
		virtual request_pass_handle<T> request_raw(bool allow_expired) = 0;

	};

	export
	template <typename T>
	type_aware_node<T>& node_type_cast(node& node) noexcept(!MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK){
#if MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
		auto idt = node.get_out_socket_type_index();
		if(idt != unstable_type_identity_of<T>()){
			throw invalid_node_error{"Node type NOT match"};
		}
#endif
		return static_cast<type_aware_node<T>&>(node);
	}


	export
	template <typename T>
	struct provider_general : type_aware_node<T>{
		static constexpr data_type_index node_data_type_index = unstable_type_identity_of<T>();
		friend manager;

		[[nodiscard]] provider_general() = default;


		[[nodiscard]] explicit provider_general(manager& manager) : manager_(std::addressof(manager)){
		}

	protected:
		[[nodiscard]] explicit provider_general(manager& manager, propagate_behavior propagate_type)
			: type_aware_node<T>{propagate_type}, manager_(std::addressof(manager)){
		}

	public:
		//TODO should these two function virtual?

		void update_value(T&& value){
			this->update_value_unchecked(std::move(value));
		}

		void update_value(const T& value){
			this->update_value_unchecked(T{value});
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{&node_data_type_index, 1};
		}


		void disconnect_self_from_context() noexcept final{
			for(const auto& successor : successors){
				successor.entity->erase_predecessor_single_edge(successor.index, *this);
			}
			successors.clear();
		}

		[[nodiscard]] std::span<const successor_entry> get_outputs() const noexcept final{
			return successors;
		}

	private:
		void update_value_unchecked(T&& value){
			this->on_push(std::addressof(value));
		}

		bool connect_successors_impl(const std::size_t slot, node& post) final{
			if(auto& ptr = post.get_inputs()[slot]){
				post.erase_predecessor_single_edge(slot, *ptr);
			}
			return node::try_insert(successors, slot, post);
		}

		bool erase_successors_single_edge(std::size_t slot, node& post) noexcept final{
			return node::try_erase(successors, slot, post);
		}

	protected:
		manager* manager_{};
		std::vector<successor_entry> successors{};

		virtual void on_push(void* in_data){
			T* target = static_cast<T*>(in_data);
			std::size_t count = successors.size();
			for(std::size_t i = 0; i < count; ++i){
				if(i == count - 1){
					push_data_storage<T> data(std::move(*target));
					successors[i].update(data);
				} else{
					push_data_storage<T> data(*target);
					successors[i].update(data);
				}
			}
		}

	};

	export
	template <typename T>
	struct terminal_cached;

	template <typename T>
	struct terminal : type_aware_node<T>{
	private:
		raw_node_ptr parent{};

	protected:
		[[nodiscard]] explicit terminal(propagate_behavior data_propagate_type)
			: type_aware_node<T>(data_propagate_type){
		}

	public:
		[[nodiscard]] terminal() = default;


		static constexpr data_type_index node_data_type_index = unstable_type_identity_of<T>();

		[[nodiscard]] std::span<const raw_node_ptr> get_inputs() const noexcept final{
			return {&parent, 1};
		}

		[[nodiscard]] std::span<const data_type_index> get_in_socket_type_index() const noexcept final{
			return std::span{&node_data_type_index, 1};
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			return this->parent->get_data_state();
		}

		void check_expired_and_update(bool allow_expired){
			if(!this->is_data_expired()) return;

			if(auto rst = this->request(allow_expired)){
				this->on_update(rst.value().fetch());
			}
		}

		void disconnect_self_from_context() noexcept final{
			if(parent){
				parent->erase_successors_single_edge(0, *this);
				parent = nullptr;
			}
		}

		request_pass_handle<T> request_raw(const bool allow_expired) override{
			assert(parent != nullptr);
			return node_type_cast<T>(*parent).request_raw(allow_expired);
		}

	protected:
		void connect_predecessor_impl(const std::size_t slot, node& prev) final{
			assert(slot == 0);
			if(parent){
				const auto rng = parent->get_outputs();
				const auto idx = std::ranges::distance(rng.begin(),
					std::ranges::find(rng, this, &successor_entry::get));
				parent->erase_successors_single_edge(idx, *this);
			}
			parent = std::addressof(prev);
			if(auto dat = this->nothrow_request(true)){
				this->on_update(*dat);
			}
		}

		void erase_predecessor_single_edge(std::size_t slot, node& prev) noexcept final{
			assert(slot == 0);
			if(parent == std::addressof(prev)) parent = {};
		}


		virtual void on_update(const T& data){
		}

		void mark_updated(const std::size_t from_index) noexcept final{
			assert(from_index == 0);
			this->data_pending_state_ = data_pending_state::expired;
		}

		friend terminal_cached<T>;

		void on_push(const std::size_t from_index, push_data_obj& in_data) override{
			assert(from_index == 0);
			this->data_pending_state_ = data_pending_state::done;
			auto& storage = push_data_cast<T>(in_data);
			this->on_update(storage.get());
		}
	};

	export
	template <typename T>
	struct terminal_cached : terminal<T>{
		[[nodiscard]] const T& request_cache(){
			update_cache();
			return cache_;
		}

		[[nodiscard]] data_state get_data_state() const noexcept override{
			if(this->is_data_expired()) return data_state::expired;
			return data_state::fresh;
		}

		[[nodiscard]] explicit terminal_cached(propagate_behavior data_propagate_type)
			: terminal<T>(data_propagate_type){
		}

		[[nodiscard]] terminal_cached() = default;

		request_pass_handle<T> request_raw(const bool allow_expired) final{
			update_cache();

			if(!this->is_data_expired() || allow_expired){
				return react_flow::make_request_handle_expected_ref(cache_, this->is_data_expired());
			} else{
				return react_flow::make_request_handle_unexpected<T>(data_state::expired);
			}
		}

	protected:
		void on_update(const T& data) override{
			terminal<T>::on_update(data);
		}

		void on_pulse_received(manager& m) override{
			if(this->data_pending_state_ != data_pending_state::waiting_pulse) return;
			this->data_pending_state_ = data_pending_state::done;
			this->on_update(cache_);
		}

		void on_push(const std::size_t from_index, push_data_obj& in_data) override{
			switch(this->data_propagate_type_){
			case propagate_behavior::eager : this->data_pending_state_ = data_pending_state::done;
				{
					auto& storage = push_data_cast<T>(in_data);
					cache_ = storage.get();
				}
				this->on_update(cache_);
				break;
			case propagate_behavior::lazy : this->data_pending_state_ = data_pending_state::expired;
				break;
			case propagate_behavior::pulse : this->data_pending_state_ = data_pending_state::waiting_pulse;
				{
					auto& storage = push_data_cast<T>(in_data);
					cache_ = storage.get();
				}
				break;
			default : std::unreachable();
			}
		}

	private:
		T cache_{};

		void update_cache(){
			if(this->is_data_expired()){
				if(auto rst = this->terminal<T>::request_raw(false)){
					this->data_pending_state_ = data_pending_state::done;
					cache_ = rst.value().fetch().value();
					this->on_update(cache_);
				}
			}
		}
	};


	void node_pointer::incr_() const noexcept{
		node_->incr_ref();
	}

	void node_pointer::decr_() const noexcept{
		if(node_->decr_ref()){
			node_->disconnect_self_from_context();
			delete node_;
		}
	}

	template <typename T>
	void successor_entry::update(push_data_storage<T>& data) const{
		assert(unstable_type_identity_of<T>() == entity->get_in_socket_type_index()[index]);
		entity->on_push(index, data);
	}

	void successor_entry::update(push_data_obj& data, data_type_index checker) const{
#if MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
		if(entity->get_in_socket_type_index()[index] != checker){
			throw invalid_node_error{"Type Mismatch on update"};
		}
#endif
		entity->on_push(index, data);
	}

	void successor_entry::mark_updated() const{
		entity->mark_updated(index);
	}


	bool is_reachable(const node* start_node, const node* target_node, std::unordered_set<const node*>& visited){
		if(start_node == nullptr) return false;
		if(start_node == target_node){
			return true;
		}

		visited.insert(start_node);

		for(auto& neighbor : start_node->get_inputs()){
			if(!visited.contains(neighbor)){
				if(is_reachable(neighbor, target_node, visited)){
					return true;
				}
			}
		}

		return false;
	}

	bool is_ring_bridge(const node* self, const node* successors){
		if(self == successors){
			return true;
		}

		std::unordered_set<const node*> visited;
		return is_reachable(self, successors, visited);
	}
}
