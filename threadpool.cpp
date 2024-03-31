#include "threadpool.h"
#include <iostream>
#include <functional>
#include<thread>
#include<chrono>
const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THREADHHOLD = 100;
// 线程池构造 列表初始化
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, idelThreadSize_(0)
	, curTthreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THREADHHOLD)
	, PoolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{}

// 析构  出现构造就要析构
ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	 
	notEmpty_.notify_all(); // 唤醒等待线程 全部唤醒了 只有一个抢到锁，其他的阻塞。
	// 而析构函数在wait--放锁(拿到锁发现还有线程又wait)，阻塞的线程依次抢锁，然后先isRunning判断 直接挂，不用到wait

	// 等待线程返回：阻塞 正在执行中
	std::unique_lock<std::mutex> lock(taskQueMxt_);
	//notEmpty_.notify_all(); // 唤醒等待线程
	exitCond_.wait(lock, [&]()->bool {return curTthreadSize_ == 0; });   // 等待所有线程结束
}

// 检测是否在工作
bool ThreadPool::checkRunningState() const
{
	if (isPoolRunning_)
		return true;
	else return false;
}



//设置工作模式
void ThreadPool::setMode(PoolMode mode)
{
	// 确保start之后不能设置
	if (checkRunningState())
		return;
	PoolMode_ = mode;
}

// 设置cached模式下最大线程数
void ThreadPool::setThreadSizeThreshHold(int threadSize)
{
	if (checkRunningState())  // 正在运行中 不允许设置了
		return;
	if (PoolMode_ == PoolMode::MODE_FIXED)  // fixed模式 设置干嘛呢
		return;
	threadSizeThreshHold_ = threadSize;
}

// 设置任务上线阈值
void ThreadPool::setTaskQueMaxThreshHold(int threadhold)
{
	taskQueMaxThreshHold_ = threadhold;
}

// 提交任务 用户生产任务 
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMxt_);
	
	// 满了 等待任务队列有空余
	// 用户提交任务最长不能阻塞超过1s 否则提交失败 分布式微服务中服务降级
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lock);// 进入等待且解锁
	//}
	// wait一直等到条件  wait_for最多等...  wait_until等到下周 
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]() {return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))  // 传入lambda表达式 &捕获外部变量
	{
		// 不能一直等待啊 返回false  即1s还是满的，提交失败
		std::cerr << "task queue is full, submit task fail" << std::endl;
		//return;   // Task Result
		return Result(sp,false);
	}
	// 有空余 放入任务队列 
	taskQue_.emplace(sp);
	taskSize_++;

	//通知notEmpt 可以拿任务了
	notEmpty_.notify_all();

	//cache模式 任务紧急，都是小而快的任务，需要根据任务多而增加线程
	// 人多来了很多 空闲线程少----当前线程<最大容量线程
	/*
	cached模式下  提交的任务太多线程太少，创建线程
	*/
	if (PoolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idelThreadSize_
		&& curTthreadSize_ < threadSizeThreshHold_
		)
	{
		// 创建新线程
		//threads_.emplace_back( std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc,this)) );
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		auto threadIndex = ptr->getThreadId();
		threadsMap_.emplace(threadIndex,std::move(ptr));
		threadsMap_[threadIndex]->start();
		curTthreadSize_++;
		idelThreadSize_++;

	}

	return Result(sp);
}

// 严阵以待  传入默认值 减少用户的操作，用我的不要有那么大的负担
void ThreadPool::start(int initThreshSize)
{
	// 设置线程池启动状态
	isPoolRunning_ = true;
	// 记录初始线程个数
	initThreadSize_ = initThreshSize;
	curTthreadSize_ = initThreshSize;

	// 创建线程对象
	for (int i = 0; i < initThreadSize_; ++i)
	{
		// 线程池创建 需要线程池提供线程启动函数
		// 但如何传入？ bind--将可调用对象绑定为仿函数，参数可减少
		// auto f = std::bind(可调用对象，绑定的参数/占位符)
		// auto f = std::bind(类函数/成员地址，类实例对象地址，绑定参数/占位符)
		// palceholders::_1 第一个实参 就有了参数
		// bind(output,palceholders::_1,2)(10); 第一个实参 第二个为2
		auto ptr = std::make_unique<Thread>( std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1) );
		//auto ptr = new Thread(std::bind(&ThreadPool::threadFunc, this));
		//threads_.emplace_back( new Thread(std::bind(&ThreadPool::threadFunc,this))); // 有参构造 传入this 即类实例对象地址，无参数
		//threads_.emplace_back(std::move(ptr));   // unique_ptr 不允许拷贝和赋值 做资源转移

		//insert 和emplace插入一样，只允许一个key insert用part<,> emplace直接插入
		auto threadId = ptr->getThreadId();
		threadsMap_.emplace(threadId,std::move(ptr));
		// bind之后得到的是仿函数，而接受的是包装器类型，做了隐式转换
	}
	// 启动每个线程
	for (int i = 0; i < initThreadSize_; ++i)
	{
		threadsMap_[i]->start();   // key：value即Thread
		idelThreadSize_++;  //记录初始的空闲线程
	}
}


// 定义线程函数
// 线程从任务队列里面消费任务
void ThreadPool::threadFunc(size_t threadId)
{
	/*std::cout << "start " << std::this_thread::get_id() << std::endl;
	std::cout << "end "  << std::this_thread::get_id() << std::endl;*/
	// 不断取任务  线程结束，还是需要把当前所有任务队列中任务做完
	unsigned int  SleepCount = 0;  // 计数空闲线程睡眠了多少次
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMxt_);
			std::cout << std::this_thread::get_id() << " trying to get task " << std::endl;
			// 等待notEmpty
			//notEmpty_.wait(lock, [&]() ->bool{return taskQue_.size() > 0; });
			while(taskQue_.size() == 0)  // 双重判断，避免退出时还在wait造成死锁
			{
				if (!isPoolRunning_)
				{
					// 结束运行 线程退出
					//threads_.erase();  // 如何退出？ 不知道这个Thread是哪一个啊 用unordermap
					threadsMap_.erase(threadId);
					curTthreadSize_--;
					idelThreadSize_--;
					std::cout << std::this_thread::get_id() << " wait over and exiting... " << std::endl;
					// 通知一下 可以不用等待
					exitCond_.notify_all();
					return;
				}

				// 如果是cacahed模式 空闲线程都在等待任务，需要减少
				if (PoolMode_ == PoolMode::MODE_CACHED
					&&curTthreadSize_>initThreadSize_
					)
				{
					//等一会 起床看看可以干活吗 or 被唤醒看看可以干活不
					// 判断是超时唤醒 还是 被任务唤醒
					std::cout << std::this_thread::get_id() << " cached waiting... " << std::endl;
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(3)))  // 等待并放锁
					{
						// 你都醒了10次了 还没有拿到任务，自杀吧

						SleepCount++;
						if (SleepCount >= 5 && curTthreadSize_ > initThreadSize_)
						{
							// 线程退出啦
							threadsMap_.erase(threadId);
							curTthreadSize_--;
							idelThreadSize_--;
							std::cout << std::this_thread::get_id() << " threads too many and exiting... " << std::endl;
							if (!isPoolRunning_)
								exitCond_.notify_all();
							SleepCount = 0;
							return;
						}

					}

				}
				else   // fixed 线程死等不自杀
				{
					std::cout << "waiting.........." << std::endl;
					notEmpty_.wait(lock);  // 结束等待：有任务 or 析构--->就绪态重新抢锁
				}
			}


			std::cout << std::this_thread::get_id() << " geting task successfully " << std::endl;
			// 取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 消费了还是>0 来人还可以消费
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}
			// 取出一个任务就通知
			notFull_.notify_all();

			// 拿了任务就解锁 因此更改作用域
		}
		if (task != nullptr)
		{
			// 执行任务	 把任务返回值setVal
			//task->run();
			//std::cout << std::this_thread::get_id() << " starting task " << std::endl;
			SleepCount = 0;
			idelThreadSize_--;  //空闲线程开始工作
			task->exec();
			idelThreadSize_++;  //处理完了 空闲
		}
		/* 根据需求---是把所有任务都处理完才能退出的话 */
#if  0
		if (!isPoolRunning_)
		{
			threadsMap_.erase(threadId);
			curTthreadSize_--;
			idelThreadSize_--;
			exitCond_.notify_all();
			std::cout << std::this_thread::get_id() << " working over and exiting... " << std::endl;
			return;
		}
#endif //  0
	}
}
int ThreadPool::minThreadSize_ = std::thread::hardware_concurrency();  // 最小线程数为当前核心数
///////////task方法实现


Task::Task()
	:result_(nullptr)
{}
void Task::exec()
{
	//run();  //这里发生多态 返回any类型
	if(result_ != nullptr) // 如果用户没有来接收Result
	{
		result_->setVal(run());
		// 拿一个result对象过来，调用 在task类中存一个result指针
	}
	else
	{
		std::cout << "不需要返回值" << std::endl;
		run();  // 直接run
	}
}

void Task::setResult(Result* res)
{
	result_ = res; // result里面拿到task 调用task->setResult(this)
}

/////////////////////////线程创建

size_t Thread::threadsCount =0;  // key:value
// 构造
Thread::Thread(ThreadFunc func)
	:func_(func)    // 接受bind的仿函数 保存
{
	threadsKey = threadsCount;   // 每个thread都有一个key 
	threadsCount++;   
}

size_t Thread::getThreadId()
{
	return threadsKey;
}
// 析构
Thread::~Thread()
{
}

// 创建线程 传入函数
void Thread::start()
{
	// 创建线程t 传入可调用对象func_
	std::thread t(func_,threadsKey);  // C++来说 线程对象t 和线程函数func_
	t.detach(); //分离线程 防止出了函数就挂掉 pthread_detach
}


//////////////Result的实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
	:task_(task)
	,isValid_(isValid)
{
	task_->setResult(this);  // 告诉这个任务 他的返回值需要给到这个Result
}

AnyType Result::get()  // 用户调用
{
	if (!isValid_)  // 是否有效的 不是返回空
	{
		return "";
	}
	// 等待信号量
	sem_.wait();  // 任务没有执行完 阻塞用户线程
	return std::move(any_);
}

void Result::setVal(AnyType any)
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post();  // 已经有了返回值了
}