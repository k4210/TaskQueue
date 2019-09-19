#pragma once

#include <vector>
#include <functional>
#include <array>
#include <optional>
#include <chrono>
#include <assert.h>
#include <algorithm>

#ifndef STAT
#define STAT 1
#endif

namespace TQ
{
	using TMicrosecond = std::chrono::microseconds;
	using TCategory = uint8_t;

	enum class EPriority : uint8_t
	{
		SkipAfter16Frames,
		CanWait,
		Immediate,
	};

	struct ID
	{
	private:
		uint16_t data = 0;
	public:
		static ID New()
		{
			static uint16_t id = 0;
			id++;
			ID result;
			result.data = id;
			return result;
		}
		bool operator==(const ID& other) const {return data == other.data; }
		bool IsValid() const { return !!data; }
	};

	struct TaskInfo
	{
		ID id;
		TCategory category = 0;
		EPriority priority = EPriority::CanWait;
		bool IsValid() const { return id.IsValid(); }
	};

	namespace details
	{
		template<class... Args> struct Receiver
		{
			TaskInfo info;
			std::function<void(Args...)> delegate_func;

			Receiver() {}
			Receiver(std::function<void(Args...)>&& func, TaskInfo ti)
				: delegate_func(std::move(func)), info(ti) {}
			Receiver(std::function<void(Args...)>&& func
				, ID id, TCategory category, EPriority priority)
				: delegate_func(std::move(func))
				, info{ id, category, priority }
			{}
		};

		struct Task
		{
			TaskInfo info;
			uint32_t source_frame = 0;
			std::function<void()> delegate_func;
			Task* next = nullptr;
		};

		struct SLList
		{
			Task* head = nullptr;
			Task* tail = nullptr;
			uint32_t size = 0;
		public:
			bool AnyElement() const { return nullptr != head; }
			void PushBack(Task& task)
			{
				assert(!task.next);
				if (!tail)
				{
					assert(!head);
					head = &task;
					tail = &task;
				}
				else
				{
					assert(!tail->next);
					tail->next = &task;
					tail = &task;
				}
				size++;
				assert(!tail->next);
			}
			void PushFront(Task& task)
			{
				assert(!task.next);
				if (!head)
				{
					assert(!tail);
					head = &task;
					tail = &task;
				}
				else
				{
					task.next = head;
					head = &task;
				}
				size++;
				assert(!tail->next);
			}
			Task& PopFront()
			{
				assert(AnyElement());
				if (tail == head)
				{
					tail = nullptr;
				}
				Task& result = *head;
				head = head->next;
				result.next = nullptr;
				size--;
				return result;
			}
			uint32_t GetSize() const { return size; }
			struct Iterator
			{
			private:
				SLList& list;
				SLList& free_list;
				Task** local_head = nullptr;
			public:
				Iterator(SLList& in_list, SLList& in_free_list)
					: list(in_list), free_list(in_free_list)
					, local_head(&in_list.head) {}

				Task* Get()
				{
					return local_head ? *local_head : nullptr;
				}

				void Advance()
				{
					if (local_head && *local_head)
					{
						local_head = &((*local_head)->next);
					}
				}

				void Remove()
				{
					if (local_head && *local_head)
					{
						Task& removed = **local_head;
						if (*local_head == list.tail)
						{
							if (list.head != *local_head)
							{
								const std::size_t offset = offsetof(Task, next);
								Task* prev = reinterpret_cast<Task*>
									(reinterpret_cast<int8_t*>(local_head) - offset);
								list.tail = prev;
							}
							else
							{
								assert(!removed.next);
								list.tail = nullptr;
							}
						}
						*local_head = removed.next;
						removed = Task{};
						free_list.PushFront(removed);
						list.size--;
					}
				}
			};
		};

		struct TasksPerCategory
		{
			SLList immediate_queue;
			SLList can_wait_queue;
			uint32_t GetSize() const
			{
				return immediate_queue.GetSize()
					+ can_wait_queue.GetSize();
			}
			SLList& GetForPriority(EPriority priority)
			{
				switch (priority)
				{
				case EPriority::Immediate:	return immediate_queue;
				default:					return can_wait_queue;
				}
			}
		};
	}

	namespace statistic
	{
#if STAT
		struct StatPerCategory
		{
			uint32_t done_base_time = 0;
			uint32_t pending = 0;
			uint32_t done_additional_time = 0;
			uint32_t skipped = 0;
			TMicrosecond remaining_time{ 0 };
		};

		template<uint32_t kCategoryNum> struct Stats
		{
			std::array<StatPerCategory, kCategoryNum> stats;
			void FillBaseTime(const std::array<TMicrosecond, kCategoryNum>& remaining)
			{
				for (uint32_t idx = 0; idx < kCategoryNum; idx++)
				{
					stats[idx].remaining_time = remaining[idx];
				}
			}

			void FillPending(std::array<details::TasksPerCategory, kCategoryNum>& tasks)
			{
				for (uint32_t idx = 0; idx < kCategoryNum; idx++)
				{
					stats[idx].pending = tasks[idx].GetSize();
				}
			}

			void Print(uint32_t frame, const std::array<TMicrosecond, kCategoryNum>& budgets)
			{
				printf("Frame: %d\n", frame);
				printf("Cat.: \tDone: \tAdd.: \tRem.: \tSkip: \tRem Time: \tBudget:\n");
				for (uint32_t idx = 0; idx < kCategoryNum; idx++)
				{
					const auto& s = stats[idx];
					printf("  %3d \t%5d \t%5d \t%5d \t%5d\t%+3.3f  \t%+3.3f [ms]\n"
						, idx, s.done_base_time, s.done_additional_time, s.pending, s.skipped
						, s.remaining_time.count() / 1000.0f
						, budgets[idx].count() / 1000.0f);
				}
			}

			void DoneBaseTime(details::Task& task)
			{
				stats[task.info.category].done_base_time++;
			}

			void DoneAdditionalTime(details::Task& task)
			{
				stats[task.info.category].done_additional_time++;
			}

			void Skipped(details::Task& task)
			{
				stats[task.info.category].skipped++;
			}
		};
#else
		template<uint32_t kCategoryNum> struct Stats
		{
			void FillBaseTime(const std::array<TMicrosecond, kCategoryNum>&) {}
			void FillPending(std::array<details::TasksPerCategory, kCategoryNum>&) {}
			void Print(uint32_t, const std::array<TMicrosecond, kCategoryNum>&) {}
			void DoneBaseTime(details::Task&) {}
			void DoneAdditionalTime(details::Task&) {}
			void NotDone(details::Task&) {}
			void Skipped(details::Task&) {}
		};
#endif
	}

	template<uint32_t kCategoryNum> class TaskQueue
	{
		static const uint32_t kPoolSize = 1024;
		details::SLList free_list;
		std::array<details::Task, kPoolSize> pool;
		std::array<details::TasksPerCategory, kCategoryNum> tasks;
		std::array<TMicrosecond, kCategoryNum> budgets;
		std::vector<TaskInfo> to_remove;
		uint32_t frame = 0;
		uint32_t last_idx = 0;

		TaskQueue()
		{
			free_list.head = &pool[0];
			free_list.tail = free_list.head;
			for (int32_t idx = 1; idx < kPoolSize; idx++)
			{
				details::Task* ptr = &pool[idx];
				free_list.tail->next = ptr;
				free_list.tail = ptr;
			}
		}
		TaskQueue(const TaskQueue&) = delete;
		TaskQueue(TaskQueue&&) = delete;

		void RemovePending()
		{
			auto remove_from_list = [&](TaskInfo info, details::SLList& list)
			{
				for (auto iter = details::SLList::Iterator(list, free_list); iter.Get(); )
				{
					if (iter.Get()->info.id == info.id) iter.Remove();
					else iter.Advance();
				}
			};

			for (const auto& it : to_remove)
			{
				details::SLList& list = tasks[it.category].GetForPriority(it.priority);
				remove_from_list(it, list);
			}

			to_remove.clear();
		}
	public:
		static TaskQueue& Get()
		{
			static TaskQueue<kCategoryNum> sInstance;
			return sInstance;
		}
		void SetBudget(TCategory category, TMicrosecond value)
		{
			assert(category < kCategoryNum);
			budgets[category] = value;
		}
		void AddTask(TaskInfo info, std::function<void()>&& delegate_func)
		{
			assert(free_list.AnyElement());
			details::Task& task = free_list.PopFront();
			task.delegate_func = std::move(delegate_func);
			task.info = info;
			task.source_frame = frame;
			tasks[info.category].GetForPriority(info.priority).PushBack(task);
		}
		void Remove(TaskInfo info)
		{
			to_remove.push_back(info);
		}
		void ExecuteTick(TMicrosecond whole_tick_time)
		{
			RemovePending();

			statistic::Stats<kCategoryNum> stats;
			auto get_time = []() -> TMicrosecond
			{
				return std::chrono::duration_cast<TMicrosecond>(
					std::chrono::system_clock::now().time_since_epoch());
			};

			std::array<TMicrosecond, kCategoryNum> local_budgets = budgets;
			const TMicrosecond start_time = get_time();
			TMicrosecond current_time = start_time;
			auto update_time = [&](TMicrosecond& budget)
			{
				auto old_time = current_time;
				current_time = get_time();
				budget -= current_time - old_time;
			};
			auto has_time = [&]() { return (get_time() - start_time) < whole_tick_time; };

			for (uint32_t idx = 0; idx < kCategoryNum; idx++)
			{
				for (auto iter = details::SLList::Iterator(tasks[idx].immediate_queue, free_list); iter.Get(); )
				{
					details::Task& task = *iter.Get();
					task.delegate_func();
					stats.DoneBaseTime(task);
					iter.Remove();
				}
				update_time(local_budgets[idx]);
			}

			for (uint32_t idx = 0; idx < kCategoryNum; idx++)
			{
				auto& local_budget = local_budgets[idx];
				for (auto iter = details::SLList::Iterator(tasks[idx].can_wait_queue, free_list); iter.Get(); )
				{
					if (local_budget <= TMicrosecond::zero())
						break;

					details::Task& task = *iter.Get();
					if ((task.info.priority == EPriority::SkipAfter16Frames)
						&& (frame - task.source_frame) > 16)
					{
						stats.Skipped(task);
						iter.Remove();
						continue;
					}

					task.delegate_func();
					stats.DoneBaseTime(task);
					iter.Remove();
					update_time(local_budget);
				}
			}

			const uint32_t base_idx = last_idx;
			for (uint32_t offset = 1; (offset <= kCategoryNum) && has_time(); offset++)
			{
				const uint32_t idx = (base_idx + offset) % kCategoryNum;
				for (auto iter = details::SLList::Iterator(tasks[idx].can_wait_queue, free_list); iter.Get() && has_time(); )
				{
					details::Task& task = *iter.Get();
					if ((task.info.priority == EPriority::SkipAfter16Frames) && (frame - task.source_frame) > 16)
					{
						stats.Skipped(task);
						iter.Remove();
						continue;
					}
					task.delegate_func();
					stats.DoneAdditionalTime(task);
					iter.Remove();
				}
				last_idx = idx;
			}
			stats.FillBaseTime(local_budgets);
			stats.FillPending(tasks);
			stats.Print(frame, budgets);
			frame++;
		}
	};

	template<class... Args> class Sender
	{
		using TReceiver = details::Receiver<Args...>;
		std::optional<TReceiver> receiver;
	public:
		bool IsSet() const { receiver.has_value(); }
		void Reset() { receiver.reset(); }
		TaskInfo GetTaskInfo() const
		{
			return receiver.has_value() ? receiver->info : TaskInfo{};
		}
		Sender() {}
		Sender(const Sender& other) : receiver(other.receiver) {}
		Sender(std::function<void(Args...)>&& func, TCategory category
			, EPriority priority = EPriority::CanWait)
			: receiver(std::in_place, std::move(func), ID::New(), category, priority)
		{}
		Sender& operator=(const Sender& other)
		{
			receiver = other.receiver;
		}
		Sender& operator=(Sender&& other)
		{
			receiver = std::move(other.receiver);
			return *this;
		}

		template<typename TTQ> void Send(TTQ& tq, Args... args) const
		{
			if(receiver.has_value())
			{
				tq.AddTask(receiver->info
					, std::bind(receiver->delegate_func, args...));
			}
		}

		template<typename TTQ> void RemovePendingTask(TTQ& tq) const
		{
			if (receiver.has_value())
			{
				tq.Remove(receiver->info);
			}
		}
	};

	template<class... Args> class SenderMultiCast
	{
		using TReceiver = details::Receiver<Args...>;
		std::vector<TReceiver> receivers;

	public:
		 
		TaskInfo Register(std::function<void(Args...)>&& func
			, TCategory category
			, EPriority priority = EPriority::CanWait)
		{
			TaskInfo info{ ID::New(), category, priority };
			receivers.emplace_back(std::move(TReceiver(std::move(func), info)));
			return info;
		}

		uint32_t UnRegister(ID receiver_id)
		{
			uint32_t counter = 0;
			for (auto it = receivers.begin(); it != receivers.end(); ) 
			{
				if (receivers[it].info.receiver_id == receiver_id)
				{
					counter++;
					it = receivers.erase(it);
				}
				else
				{
					it++;
				}
			}
			return counter;
		}

		template<typename TTQ> void Send(Args... args) const
		{
			for (const auto& receiver : receivers)
			{
				TTQ::Get().AddTask(receiver.info, std::bind(receiver.delegate_func, args...));
			}
		}

		template<typename TTQ> void RemovePendingTasks() const
		{
			for (const auto& receiver : receivers)
			{
				TTQ::Get().Remove(receiver.info);
			}
		}
	};
};