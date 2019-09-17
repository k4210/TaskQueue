#pragma once

#include <vector>
#include <functional>
#include <array>
#include <optional>
#include <chrono>
#include <assert.h>

namespace TQ
{
	enum class ECategory : uint8_t
	{
		Unknown,
		A,
		B,
		C,
		_Count
	};

	constexpr uint8_t CategoryToInt(ECategory c) { return static_cast<uint8_t>(c); }

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
		ECategory category = ECategory::Unknown;
		EPriority priority = EPriority::CanWait;
		bool IsValid() const { return id.IsValid(); }
	};

	using TMicrosecond = std::chrono::microseconds;

	class TaskQueue
	{
		static const uint32_t kCategoryNum = CategoryToInt(ECategory::_Count) + 1;
		struct Task
		{
			TaskInfo info;
			uint32_t source_frame = 0;
			std::function<void()> delegate_func;
			Task* next = nullptr;
		};

		static const uint32_t kPoolSize = 1024;
		std::array<Task, kPoolSize> pool;

		struct SLList
		{
			Task* head = nullptr;
			Task* tail = nullptr;

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
				return result;
			}

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
								Task* prev = reinterpret_cast<Task*>(reinterpret_cast<int8_t*>(local_head) - offset);
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
					}
				}
			};
		};

		SLList free_list;
		SLList immediate_queue;
		SLList can_wait_queue;
		SLList skip_after_16;

		std::array<TMicrosecond, kCategoryNum> budgets;
		std::array<std::vector<TaskInfo>, 3> to_remove;
		uint32_t frame = 0;

		void InitializeFreeList()
		{
			free_list.head = &pool[0];
			free_list.tail = free_list.head;
			for (int32_t idx = 1; idx < kPoolSize; idx++)
			{
				Task* ptr = &pool[idx];
				free_list.tail->next = ptr;
				free_list.tail = ptr;
			}
		}
		SLList& GetList(EPriority priority);

		TaskQueue();
		TaskQueue(const TaskQueue&) = delete;
		TaskQueue(TaskQueue&&) = delete;

		void RemovePending(SLList& list, std::vector<TaskInfo>& pending);
	public:
		static TaskQueue& Get();
		void AddTask(TaskInfo info, std::function<void()>&& delegate_func);
		void Remove(TaskInfo Info);
		void ExecuteTick(TMicrosecond whole_tick_time);
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
				, ID id, ECategory category, EPriority priority) 
				: delegate_func(std::move(func))
				, info{ id, category, priority }
			{}
		};
	}

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
		Sender(std::function<void(Args...)>&& func
			, ECategory category = ECategory::Unknown
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

		void Send(Args... args) const
		{
			if(receiver.has_value())
			{
				TaskQueue::Get().AddTask(receiver->info
					, std::bind(receiver->delegate_func, args...));
			}
		}

		void RemovePendingTask() const
		{
			if (receiver.has_value())
			{
				TaskQueue::Get().Remove(receiver->info);
			}
		}
	};

	template<class... Args> class SenderMultiCast
	{
		using TReceiver = details::Receiver<Args...>;
		std::vector<TReceiver> receivers;

	public:
		 
		TaskInfo Register(std::function<void(Args...)>&& func
			, ECategory category = ECategory::Unknown
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

		void Send(Args... args) const
		{
			for (const auto& receiver : receivers)
			{
				TaskQueue::Get().AddTask(receiver.info
					, std::bind(receiver.delegate_func, args...));
			}
		}

		void RemovePendingTasks() const
		{
			for (const auto& receiver : receivers)
			{
				TaskQueue::Get().Remove(receiver.info);
			}
		}
	};
};