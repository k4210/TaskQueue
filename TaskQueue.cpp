#include "TaskQueue.h"
#include <algorithm>

using namespace TQ;

TaskQueue& TaskQueue::Get()
{
	static TaskQueue sInstance;
	return sInstance;
}

TaskQueue::TaskQueue()
{
	InitializeFreeList();

	budgets[CategoryToInt(ECategory::Unknown)]	= TMicrosecond{ 500 };
	budgets[CategoryToInt(ECategory::Sample)]	= TMicrosecond{ 500 };
}

void TaskQueue::AddTask(TaskInfo info, std::function<void()>&& delegate_func)
{
	assert(free_list.AnyElement());
	Task& task = free_list.PopFront();
	task.delegate_func = std::move(delegate_func);
	task.info = info;
	task.source_frame = frame;
	switch (task.info.priority)
	{
		case EPriority::SkipAfter16Frames:	skip_after_16.PushBack(task); break;
		case EPriority::CanWait:			can_wait_queue.PushBack(task); break;
		case EPriority::Immediate:			immediate_queue.PushBack(task); break;
	}
}

void TaskQueue::Remove(TaskInfo info)
{
	switch (info.priority)
	{
		case EPriority::SkipAfter16Frames:	to_remove[0].push_back(info); break;
		case EPriority::CanWait:			to_remove[1].push_back(info); break;
		case EPriority::Immediate:			to_remove[2].push_back(info); break;
	}
}

void TaskQueue::RemovePending(SLList& list, std::vector<TaskInfo>& pending)
{
	if (!pending.size())
		return;
	for (auto iter = SLList::Iterator(list, free_list); iter.Get(); )
	{
		const ID id = iter.Get()->info.id;
		const bool found = std::find_if(pending.begin(), pending.end()
			, [id](const TaskInfo& ti) { return ti.id == id; }) != pending.end();
		if (found) iter.Remove();
		else iter.Advance();
	}
	pending.clear();
}

#define  STAT 1
#if STAT
struct StatPerCategory
{
	uint32_t done_base_time = 0;
	uint32_t pending = 0;
	uint32_t done_additional_time = 0;
	uint32_t skipped = 0;
	TMicrosecond remaining_time{ 0 };
};

struct Stats
{
	std::array<StatPerCategory, TaskQueue::kCategoryNum> stats;
	void FillBaseTime(const std::array<TMicrosecond, TaskQueue::kCategoryNum>& remaining)
	{
		for (uint32_t idx = 0; idx < TaskQueue::kCategoryNum; idx++)
		{
			stats[idx].remaining_time = remaining[idx];
		}
	}

	void Print(uint32_t frame, const std::array<TMicrosecond, TaskQueue::kCategoryNum>& budgets)
	{
		printf("Frame: %d\n", frame);
		printf("Cat.: \tDone: \tAdd.: \tRem.: \tSkip: \tRem Time: \tBudget:\n");
		for (uint32_t idx = 0; idx < TaskQueue::kCategoryNum; idx++)
		{
			const auto& s = stats[idx];
			printf("  %3d \t%5d \t%5d \t%5d \t%5d\t%+3.3f  \t%+3.3f [ms]\n"
				, idx, s.done_base_time, s.done_additional_time, s.pending, s.skipped
				, s.remaining_time.count() / 1000.0f
				, budgets[idx].count() / 1000.0f);
		}
	}

	void DoneBaseTime(TaskQueue::Task& task)
	{
		stats[CategoryToInt(task.info.category)].done_base_time++;
	}

	void DoneAdditionalTime(TaskQueue::Task& task)
	{
		auto& s = stats[CategoryToInt(task.info.category)];
		s.done_additional_time++;
		s.pending--;
	}

	void NotDone(TaskQueue::Task& task)
	{
		stats[CategoryToInt(task.info.category)].pending++;
	}

	void Skipped(TaskQueue::Task& task)
	{
		stats[CategoryToInt(task.info.category)].skipped++;
	}
};
#else
struct Stats
{
	void FillBaseTime(const std::array<TMicrosecond, kCategoryNum>&) {}
	void Print(uint32_t, const std::array<TMicrosecond, kCategoryNum>&) {}
	void DoneBaseTime(TaskQueue::Task&) {}
	void DoneAdditionalTime(TaskQueue::Task&) {}
	void NotDone(TaskQueue::Task&) {}
	void Skipped(TaskQueue::Task&) {}
};
#endif

void TaskQueue::ExecuteTick(TMicrosecond whole_tick_time)
{
	RemovePending(skip_after_16,   to_remove[0]);
	RemovePending(can_wait_queue,  to_remove[1]);
	RemovePending(immediate_queue, to_remove[2]);
	
	Stats stats;
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

	for (auto iter = SLList::Iterator(immediate_queue, free_list); iter.Get(); )
	{
		Task& task = *iter.Get();
		auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
		task.delegate_func();
		stats.DoneBaseTime(task);
		iter.Remove();
		update_time(local_budget);
	}

	if (has_time())
	{
		for (auto iter = SLList::Iterator(can_wait_queue, free_list); iter.Get(); )
		{
			Task& task = *iter.Get();
			auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
			if (local_budget <= TMicrosecond::zero())
			{
				stats.NotDone(task);
				iter.Advance();
				continue;
			}

			task.delegate_func();
			stats.DoneBaseTime(task);
			iter.Remove();
			update_time(local_budget);
		}
	}
	if (has_time())
	{
		for (auto iter = SLList::Iterator(skip_after_16, free_list); iter.Get(); )
		{
			Task& task = *iter.Get();
			if ((frame - task.source_frame) > 16)
			{
				stats.Skipped(task);
				iter.Remove();
				continue;
			}

			auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
			if (local_budget <= TMicrosecond::zero())
			{
				stats.NotDone(task);
				iter.Advance();
				continue;
			}

			task.delegate_func();
			stats.DoneBaseTime(task);
			iter.Remove();
			update_time(local_budget);
		}
	}
	stats.FillBaseTime(local_budgets);

	while (has_time() && can_wait_queue.AnyElement())
	{
		Task& task = can_wait_queue.PopFront();
		task.delegate_func();
		stats.DoneAdditionalTime(task);
		task = Task{};
		free_list.PushFront(task);
	}

	while (has_time() && skip_after_16.AnyElement())
	{
		Task& task = skip_after_16.PopFront();
		task.delegate_func();
		stats.DoneAdditionalTime(task);
		task = Task{};
		free_list.PushFront(task);
	}
	stats.Print(frame, budgets);
	frame++;
}