#include "TaskQueue.h"
#include <cstdio>
#include <memory>

using namespace TQ;

struct TArg
{
	int data = 666;
	//TArg() { printf("Arg constructor \n"); }
	//TArg(const TArg& a) : data(a.data) { printf("Arg copy constructor <--- \n"); }
	//TArg(TArg&& a) : data(a.data) { a.data = 0; printf("Arg move constructor \n"); }
	//TArg& operator=(const TArg& a) { data = a.data; printf("Arg assign operator <--- \n"); return *this; }
	//TArg& operator=(TArg&& a) { data = a.data; a.data = 0; printf("Arg move assign operator \n"); return *this; }
	//~TArg() { printf("Arg destructor \n");
};

using Arg = const TArg&;

struct TestSender
{
	SenderMultiCast<Arg> mc_delegate;
	//Sender<Arg> sc_delegate;
	void Do(Arg val)
	{
		mc_delegate.Send(val);
		//sc_delegate.Send(val);
	}
};

struct TestReceiver
{
	void Receive(Arg val)
	{
		for (int i = 0; i < 1024; i++)
		{
			atan((double)i);
		}
		//printf("Received %d\n", val.data);
	}
};

int main()
{
	TestSender s;
	TestReceiver r1, r2;
	for (int i = 0; i < 320; i++)
	{
		s.mc_delegate.Register(std::bind(&TestReceiver::Receive, r1
			, std::placeholders::_1));
		s.mc_delegate.Register(std::bind(&TestReceiver::Receive, r1
			, std::placeholders::_1), ECategory::Sample, EPriority::Immediate);
		s.mc_delegate.Register([&r2](Arg arg) { r2.Receive(arg); }
			, ECategory::Unknown, EPriority::SkipAfter16Frames);
	}
	//s.sc_delegate = Sender<Arg>(
	//	[](Arg val) { printf("Received 2 %d\n", val.data); }
	//	, ECategory::Unknown, EPriority::Immediate);

	TArg arg;
	s.Do(arg);
	for (int i = 0; i < 20; i++)
	{
		TaskQueue::Get().ExecuteTick(TMicrosecond{ 2 * 1000 });
	}
	//s.Do(arg);
	//TaskQueue::Get().ExecuteTick(TMicrosecond{ 1 * 1000 });

}