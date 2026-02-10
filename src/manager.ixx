module;

#include <version>
#define NODISCARD_ON_ADD [[nodiscard("You should save the reference to node on add")]]

export module mo_yanxi.react_flow:manager;

import :node_interface;
import mo_yanxi.utility;
import mo_yanxi.concurrent.mpsc_queue;
import mo_yanxi.concurrent.atomic_double_buffer;
import mo_yanxi.flat_set;
import mo_yanxi.algo;

namespace mo_yanxi::react_flow{
	export struct manager;

	export using progress_type = unsigned;
	export constexpr progress_type f32_to_progress_scl = std::numeric_limits<unsigned short>::max();
	export constexpr progress_type invalid_progress = std::numeric_limits<progress_type>::max();

	export struct progress_check{
		bool changed;
		progress_type progress;

		constexpr explicit operator bool() const noexcept{
			return invalid_progress == progress;
		}

		[[nodiscard]] constexpr float get_f32_progress() const noexcept{
			return static_cast<float>(progress) / static_cast<float>(f32_to_progress_scl);
		}
	};


	struct async_task_base{
		friend manager;

	private:
		bool check_during_update_{};

	public:
		[[nodiscard]] async_task_base() = default;

		explicit async_task_base(bool check_during_update)
			: check_during_update_(check_during_update){
		}

		virtual ~async_task_base() = default;

		virtual void execute(const manager& manager){
		}

		virtual void on_finish(manager& manager){
		}

		virtual node* get_owner_if_node() noexcept{
			return nullptr;
		}

		virtual progress_check get_progress() const noexcept{
			return {};
		}

		/**
		 * @brief Called on manager main thread when the async task is under processing
		 */
		virtual void on_update_check(manager& manager){

		}


	};


	struct progressed_async_node_base : async_task_base{
	private:
		mutable progress_type last_progress = invalid_progress;
		std::atomic<progress_type> progress{invalid_progress};

	public:
		using async_task_base::async_task_base;

		void set_progress_done() noexcept{
			progress.store(f32_to_progress_scl, std::memory_order_relaxed);
		}

		void set_progress(float prog) noexcept{
			progress.store(static_cast<progress_type>(std::clamp(prog, 0.f, 1.f) * static_cast<float>(f32_to_progress_scl)),
				std::memory_order_relaxed);
		}

		void set_progress(unsigned cur, unsigned total) noexcept{
			progress.store(std::min(cur * f32_to_progress_scl / total, f32_to_progress_scl), std::memory_order_relaxed);
		}

		progress_check get_progress() const noexcept override{
			//it should use relax principally, but consider that someone want to do sync depends on the progress, make it acq rel
			const auto cur = progress.load(std::memory_order_acquire);
			const auto last = std::exchange(last_progress, cur);
			return {cur != last, cur};
		}
	};

	//TODO allocator?

	export struct manager_no_async_t{
	};

	export constexpr inline manager_no_async_t manager_no_async{};

#ifdef __cpp_lib_move_only_function
	using AsyncFuncType = std::move_only_function<void()>;
#else
	using AsyncFuncType = std::function<void()>;
#endif

	//TODO support move?

	export struct manager{
	private:
		std::vector<node_pointer> nodes_anonymous_{};
		std::vector<node*> pulse_subscriber_{};
		linear_flat_set<std::vector<node*>> expired_nodes_{};

		using async_task_queue = ccur::mpsc_queue<AsyncFuncType>;
		async_task_queue pending_received_updates_{};
		async_task_queue::container_type recycled_queue_container_{};

		ccur::mpsc_queue<std::unique_ptr<async_task_base>> pending_async_modifiers_{};
		std::atomic<async_task_base*> under_processing_{};

		//TODO using small vector here?
		using done_vec_type = std::vector<std::unique_ptr<async_task_base>>;
		ccur::atomic_double_buffer<done_vec_type> async_done_buffer_{};
		done_vec_type manager_thread_done_buffer_{};

		std::jthread async_thread_{};

		template <std::derived_from<node> T, typename... Args>
		[[nodiscard]] node_pointer make_node(Args&&... args){
			return mo_yanxi::back_redundant_construct<node_pointer, 1>(std::in_place_type<T>, *this,
				std::forward<Args>(args)...);
		}
		void process_node(node& node){
			node.set_manager(*this);
			if(node.get_propagate_type() == propagate_type::pulse){
				pulse_subscriber_.push_back(&node);
			}
		}

	public:
		[[nodiscard]] manager() : async_thread_([](std::stop_token stop_token, manager& manager){
			execute_async_tasks(std::move(stop_token), manager);
		}, std::ref(*this)){
		}

		[[nodiscard]] explicit manager(manager_no_async_t){
		}

		template <std::derived_from<node> T, typename... Args>
		NODISCARD_ON_ADD T& add_node(Args&&... args){
			auto& ptr = nodes_anonymous_.emplace_back(this->make_node<T>(std::forward<Args>(args)...));
			this->process_node(*ptr);
			return static_cast<T&>(*ptr);
		}

		template <std::derived_from<node> T>
		NODISCARD_ON_ADD T& add_node(T&& node){
			auto& ptr = nodes_anonymous_.emplace_back(this->make_node<T>(std::move(node)));
			this->process_node(*ptr);
			return static_cast<T&>(*ptr);
		}

		template <std::derived_from<node> T>
		NODISCARD_ON_ADD T& add_node(const T& node){
			auto& ptr = nodes_anonymous_.emplace_back(this->make_node<T>(node));
			this->process_node(*ptr);
			return static_cast<T&>(*ptr);
		}

		void clear_isolated() noexcept{
			try{
				for(auto&& node : nodes_anonymous_){
					if(node->is_isolated()){
						expired_nodes_.insert(node.get());
					}
				}
			} catch(...){
				return; //end garbage collection directly
			}
		}

		bool erase_node(node& n) noexcept
		try{
			//TODO cancel n's async task?

			return expired_nodes_.insert(std::addressof(n));
		} catch(const std::bad_alloc&){
			pending_async_modifiers_.erase_if([&](const std::unique_ptr<async_task_base>& ptr){
				return ptr->get_owner_if_node() == &n;
			});
			algo::erase_unique_unstable(pulse_subscriber_, &n);
			return algo::erase_unique_if_unstable(nodes_anonymous_, [&](const node_pointer& ptr){
				return ptr.get() == &n;
			});
		}


		/**
		 * @brief Called from OTHER thread that need do sth on the main data flow thread.
		 */
		template <std::invocable<> Fn>
			requires (std::move_constructible<std::remove_cvref_t<Fn>>)
		void push_posted_act(Fn&& fn){
			pending_received_updates_.emplace(std::forward<Fn>(fn));
		}

		void push_task(std::unique_ptr<async_task_base> task){
			if(async_thread_.joinable()){
				pending_async_modifiers_.push(std::move(task));
			} else{
				task->execute(*this);
				task->on_finish(*this);

				if(task->check_during_update_){
					//should it check before finish?
					task->on_update_check(*this);
				}
			}
		}

		void update(){
			if(!expired_nodes_.empty()){
				pending_async_modifiers_.erase_if([&](const std::unique_ptr<async_task_base>& ptr){
					return expired_nodes_.contains(ptr->get_owner_if_node());
				});
				algo::erase_unique_if_unstable(nodes_anonymous_, [&](const node_pointer& ptr){
					return expired_nodes_.contains(ptr.get());
				});
				algo::erase_unique_if_unstable(pulse_subscriber_, [&](node* ptr){
					return expired_nodes_.contains(ptr);
				});

				expired_nodes_.clear();
			}

			for(const auto& pulse_subscriber : pulse_subscriber_){
				pulse_subscriber->on_pulse_received(*this);
			}

			if(pending_received_updates_.swap(recycled_queue_container_)){
				for(auto&& fn : recycled_queue_container_){
					fn();
				}
				recycled_queue_container_.clear();
			}


			auto cur = under_processing_.load(std::memory_order_acquire);
			if(cur && cur->check_during_update_)cur->on_update_check(*this);


			{

				async_done_buffer_.load([&](done_vec_type& vec){
					std::ranges::swap(manager_thread_done_buffer_, vec);
				});

				for(auto&& task_base : manager_thread_done_buffer_){
					task_base->on_finish(*this);

					if(task_base->check_during_update_){
						//should it check before finish?
						task_base->on_update_check(*this);
					}
				}
				manager_thread_done_buffer_.clear();

			RET:
				(void)0;
			}
		}

		std::stop_token get_manager_stop_token() const noexcept{
			return async_thread_.get_stop_token();
		}

		~manager(){
			if(async_thread_.joinable()){
				async_thread_.request_stop();
				pending_async_modifiers_.notify();
				async_thread_.join();
			}

		}

	private:
		static void execute_async_tasks(std::stop_token stop_token, manager& manager){
			while(!stop_token.stop_requested()){
				auto&& task = manager.pending_async_modifiers_.consume([&stop_token]{
					return stop_token.stop_requested();
				});

				if(task){
					manager.under_processing_.store(task.value().get(), std::memory_order_release);
					task.value()->execute(manager);
					manager.under_processing_.store(nullptr, std::memory_order_release);

					manager.async_done_buffer_.modify([&](done_vec_type& vec){
						vec.push_back(std::move(task.value()));
					});

				} else{
					return;
				}
			}
		}
	};
}
