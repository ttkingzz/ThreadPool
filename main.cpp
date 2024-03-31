#include "threadpool.h"
#include<chrono>
#include<thread>
#include<iostream>
using namespace std;
/*
有些场景 希望获得任务返回值
如： 1+...+30000的和
question 1.怎么设计run函数返回值接受任意类型
question 2.怎么知道任务执行完了？
*/
using uLong = unsigned long long;
class MyTask :public Task
{
public:
	MyTask(int begin,int end)
		:begin_(begin)
		,end_(end)
	{}
	// 怎么设计run函数的返回值，可以表示任意的类型
	// C++17 Any类型
	AnyType run() override
	{
		std::cout << "start " << std::this_thread::get_id() << std::endl;
		uLong sum = 0;
		for (int i = begin_; i <= end_; i++)
			sum += i;
		std::cout << "end "  << std::this_thread::get_id() << "subsum is " << sum<< std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(3));
		return sum;
	}
private:
	int end_;
	int begin_;
};
int main()
{
	{
		ThreadPool pool;
		// 用户设置线程池的工作模式
		pool.setMode(PoolMode::MODE_CACHED);
		// 开始启动线程池
		pool.start(2);
		pool.submitTask(std::make_shared<MyTask>(1, 2000000));
		Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 200000));
		Result res2 = pool.submitTask(std::make_shared<MyTask>(200001, 400000));
		Result res3 = pool.submitTask(std::make_shared<MyTask>(400001, 600000));
		//std::this_thread::sleep_for(std::chrono::seconds(30));
	}

	//uLong sum1 = res1.get().cast_<uLong>();
	//uLong sum2 = res2.get().cast_<uLong>();
	//uLong sum3 = res3.get().cast_<uLong>();
	//pool.~ThreadPool();
	//cout << "sum is "<< (sum1 + sum2 + sum3) << endl;
	//uLong sum = 0;
	//for (int i = 1; i <= 600000; i++)
	//	sum += i;
	//cout << "sum is " << sum << endl;
	//std::this_thread::sleep_for(std::chrono::seconds(5));
	//getchar();
}