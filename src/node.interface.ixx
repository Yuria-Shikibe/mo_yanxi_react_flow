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

#include <mo_yanxi/adapted_attributes.hpp>

export module mo_yanxi.react_flow:node_interface;
import std;

import mo_yanxi.type_register;
export import mo_yanxi.react_flow.util;

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
	using raw_node_ptr = node*;

	using push_dispatch_fptr = void(*)(node*, std::size_t, data_carrier_obj&&);

	export
	struct successor_entry{
		std::size_t index;
		node_pointer entity;
		push_dispatch_fptr push_fp;

		[[nodiscard]] successor_entry() = default;

		[[nodiscard]] successor_entry(const std::size_t index, node& entity);

		[[nodiscard]] node* get() const noexcept{
			return entity.get();
		}

		template <typename T>
		void update(data_carrier<T>&& data) const;

		void update(data_carrier_obj&& data, data_type_index checker) const;

		void mark_updated() const;
	};

	template <typename Rng, typename T>
	void push_to_successors(Rng&& range, data_carrier<T>&& data){

		if constexpr (data_carrier<T>::is_trivial){
			for(const successor_entry& e : range){
				e.update(std::move(data));
			}
		}else{
			if constexpr (!std::is_copy_constructible_v<data_carrier<T>>){
				const auto size = std::ranges::size(range);
				if (size == 0)return;
				for(const successor_entry& e : range){
					e.update(std::move(data));
					if(data.is_empty())return;
				}
			}else{
				//try to move
				const auto size = std::ranges::size(range);
				if (size == 0)return;
				if (size == 1){
					const successor_entry& e = *std::ranges::begin(range);
					e.update(std::move(data));
				}else{
					data_carrier<T> cropped{std::as_const(data)};
					auto cur = std::ranges::begin(range);
					const auto last = std::ranges::prev(std::ranges::end(range));

					while(true){
						const successor_entry& e = *cur;
						e.update(std::move(cropped));

						++cur;
						if(cur != last){
							if(cropped.is_empty()){
								cropped = data;
							}
						} else{
							break;
						}
					}

					const successor_entry& e = *cur;
					e.update(std::move(data));
				}
			}

		}
	}

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

	private:
		std::size_t reference_count_{};

#ifdef THREAD_CHECK
		std::thread::id created_thread_id_{std::this_thread::get_id()};
#endif


	protected:
		propagate_type data_propagate_type_{};
		data_pending_state data_pending_state_{};

	public:
		[[nodiscard]] node() = default;

		[[nodiscard]] explicit node(propagate_type data_propagate_type)
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

		[[nodiscard]] propagate_type get_propagate_type() const noexcept{
			return data_propagate_type_;
		}

		void set_propagate_type(propagate_type type) noexcept{
			data_propagate_type_ = type;
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

		bool connect_successor(const std::size_t slot, node& post){
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
			return prev.connect_successor(slot, *this);
		}

		bool connect_successor(node& post){
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
			return prev.connect_successor(*this);
		}

		bool disconnect_successor(const std::size_t slot, node& post) noexcept{
			if(erase_successors_single_edge(slot, post)){
				post.erase_predecessor_single_edge(slot, *this);
				return true;
			}
			return false;
		}

		bool disconnect_predecessor(const std::size_t slot, node& prev) noexcept{
			return prev.disconnect_successor(slot, *this);
		}

		bool disconnect_successor(node& post){
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
			return prev.disconnect_successor(*this);
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

	protected:
		//TODO disconnected conflicted
		virtual bool connect_successors_impl(std::size_t slot, node& post){
			return false;
		}

		virtual void connect_predecessor_impl(std::size_t slot, node& prev){
		}

#pragma endregion

	public:
		[[nodiscard]] virtual push_dispatch_fptr get_push_dispatch_fptr() const noexcept {
			return [] FORCE_INLINE (node* n, std::size_t idx, data_carrier_obj&& data){
				n->on_push(idx, std::move(data));
			};
		}

	protected:
		template <typename S>
		push_dispatch_fptr get_this_class_push_dispatch_fptr(this const S& self) noexcept{
			return [] FORCE_INLINE (node* n, std::size_t idx, data_carrier_obj&& data){
				static_cast<S*>(n)->S::on_push(idx, std::move(data));
			};
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
		virtual void on_push(std::size_t target_index, data_carrier_obj&& in_data){
		}

		/**
		 *
		 * @brief Only notify the data has changed, used for lazy nodes. Should be mutually exclusive with update when propagated.
		 *
		 */
		virtual void mark_updated(std::size_t target_index) noexcept{
			data_pending_state_ = data_pending_state::expired;
			std::ranges::for_each(get_outputs(), &successor_entry::mark_updated);
		}

	};

	/**
	 * @brief auto disconnect the node from context on destruction
	 * @tparam T node type
	 */
	export
	template <std::derived_from<node> T>
	struct node_holder{
		T node;

		~node_holder(){
			node.disconnect_self_from_context();
		}

		node_holder(const node_holder& other) = delete;
		node_holder(node_holder&& other) noexcept = default;
		node_holder& operator=(const node_holder& other) = delete;
		node_holder& operator=(node_holder&& other) noexcept = default;

		node_holder() = default;

		template <typename ...Args>
		explicit(false) node_holder(std::in_place_type_t<T>, Args&& ...args) : node(std::forward<Args>(args)...){}

		template <typename S>
		auto* operator->(this S& self) noexcept{
			return &self.node;
		}

		template <typename S>
		auto&& operator*(this S&& self) noexcept{
			return std::forward_like<S>(self.node);
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
			if(auto rst = self.request_raw(allow_expired); rst && rst.value()){
				return rst.value().get();
			} else{
				return std::nullopt;
			}
		}

		template <typename S>
		std::expected<T, data_state> request_stated(this S& self, bool allow_expired){
			if(auto rst = self.request_raw(allow_expired)){
				if(auto optal = rst.value()){
					return std::expected<T, data_state>{optal.value().get()};
				}
				return std::unexpected{data_state::failed};
			} else{
				return std::unexpected{rst.error()};
			}
		}

		template <typename S>
		std::optional<T> nothrow_request(this S& self, bool allow_expired) noexcept try{
			if(auto rst = self.request_raw(allow_expired)){
				return rst.value().get();
			}
			return std::nullopt;
		} catch(...){
			return std::nullopt;
		}

		template <typename S>
		std::expected<T, data_state> nothrow_request_stated(this S& self, bool allow_expired) noexcept try{
			if(auto rst = self.request_raw(allow_expired)){
				if(auto optal = rst.value()){
					return std::expected<T, data_state>{optal.value().get()};
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
	void successor_entry::update(data_carrier<T>&& data) const{
		assert(unstable_type_identity_of<T>() == entity->get_in_socket_type_index()[index]);
		entity->on_push(index, std::move(data));
	}

	successor_entry::successor_entry(const std::size_t index, node& entity): index(index),
		entity(entity), push_fp{entity.get_push_dispatch_fptr()}{
	}

	void successor_entry::update(data_carrier_obj&& data, data_type_index checker) const{
#if MO_YANXI_DATA_FLOW_ENABLE_TYPE_CHECK
		if(entity->get_in_socket_type_index()[index] != checker){
			throw invalid_node_error{"Type Mismatch on update"};
		}
#endif
		push_fp(entity.get(), index, std::move(data));
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
