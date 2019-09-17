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

	budgets[CategoryToInt(ECategory::Unknown)]	= TMicrosecond{ 1 };
	budgets[CategoryToInt(ECategory::A)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::B)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::C)]		= TMicrosecond{ 5000 };
	budgets[CategoryToInt(ECategory::_Count)]	= TMicrosecond{ 16000 };
}

void TaskQueue::AddTask(TaskInfo info, std::function<void()>&& delegate_func)
{
	assert(free_list.AnyElement());
	Task& task = free_list.PopFront();
	task.delegate_func = std::move(delegate_func);
	task.info = info;
	task.source_frame = frame;
	GetList(task.info.priority).PushBack(task);
}

TaskQueue::SLList& TaskQueue::GetList(EPriority priority)
{
	switch (priority)
	{
		case EPriority::SkipAfter16Frames:	return skip_after_16;
		case EPriority::CanWait:			return can_wait_queue;
		case EPriority::Immediate:
		default:
			return immediate_queue;
	}
}

void TaskQueue::Remove(TaskInfo info)
{
	switch (info.priority)
	{
	case EPriority::SkipAfter16Frames:	to_remove[0].push_back(info);
	case EPriority::CanWait:			to_remove[1].push_back(info);
	case EPriority::Immediate:			to_remove[2].push_back(info);
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

//TODO: Stats with ifdef
void TaskQueue::ExecuteTick(TMicrosecond whole_tick_time)
{
	RemovePending(skip_after_16,   to_remove[0]);
	RemovePending(can_wait_queue,  to_remove[1]);
	RemovePending(immediate_queue, to_remove[2]);
	
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

	for (auto iter = SLList::Iterator(immediate_queue, free_list); iter.Get(); )
	{
		Task& task = *iter.Get();
		auto& local_budget = local_budgets[CategoryToInt(task.info.category)];

		task.delegate_func();
		iter.Remove();
		update_time(local_budget);
	}

	for (auto iter = SLList::Iterator(can_wait_queue, free_list); iter.Get(); )
	{
		Task& task = *iter.Get();
		auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
		if (local_budget <= TMicrosecond::zero())
		{
			iter.Advance();
			continue;
		}
		
		task.delegate_func();
		iter.Remove();
		update_time(local_budget);
	}

	for (auto iter = SLList::Iterator(skip_after_16, free_list); iter.Get(); )
	{
		Task& task = *iter.Get();
		if ((frame - task.source_frame) > 16)
		{
			iter.Remove();
			continue;
		}

		auto& local_budget = local_budgets[CategoryToInt(task.info.category)];
		if (local_budget <= TMicrosecond::zero())
		{
			iter.Advance();
			continue;
		}

		task.delegate_func();
		iter.Remove();
		update_time(local_budget);
	}

	auto has_time = [&](){ return (get_time() - start_time) < whole_tick_time; };
	while (has_time() && can_wait_queue.AnyElement())
	{
		Task& task = can_wait_queue.PopFront();
		task.delegate_func();
		update_time(local_budgets[CategoryToInt(task.info.category)]);
	}

	while (has_time() && skip_after_16.AnyElement())
	{
		Task task = skip_after_16.PopFront();
		task.delegate_func();
		update_time(local_budgets[CategoryToInt(task.info.category)]);
	}

	frame++;
}