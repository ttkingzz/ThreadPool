#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include<vector>
#include<thread>
#include<queue>
#include<memory>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
// 实际开发中不用using namespace
// 

class AnyType
{
public:
	AnyType() = default;
	~AnyType() = default;

	//unique_ptr将左值引用的拷贝构造和赋值delete
	//只允许有一个 初始化or move
	AnyType(const AnyType&) = delete;
	AnyType& operator=(const AnyType&) = delete;
	AnyType(AnyType&&) = default;   // 允许右值引用
	AnyType& operator=(AnyType&&) = default; // 允许右值引用

	// 让AnyType接收
	template<typename T>
	// Base* base_ = new Dereive<T>(data)
	// 传入的值（任意类型data) --赋值-- 派生类初始化 ---- base_指向派生类
	//Any(T data):base_(new Derive<T>(data))
	//{}
	AnyType(T data)
	{
		//base_ = new Derive<T>(data) // 传入的参数存在子类中  用父类指向子类---父类指向任意类型的子类
		base_ = std::make_unique<Derive<T>>(data);
	}

	// 把存储的data_数据提取出来
	template<typename T>
	T cast_()
	{
		// base_找到指向的Derive并取出data
		// 基类指针转成子类？  一般都是子类转基类  基类指向子类没问题(父类类型少，变成子类），子类指向父类需要注意（子类类型多 变父类？）
		// RTTI  dynamic_cast<>
		//std::unique_ptr<Derive<T>> pd = dynamic_cast<std::unique_ptr<Derive<T>>>(base_);
		
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get()); // base_.get()智能指针获取裸指针  
		// 什么时候转失败？ base_本来指向的是int类型的data 而强转为其他的如long
		if (pd == nullptr)  // 类型不匹配
		{
			throw "type is unmatched ";
		}
		return pd->data_;

	}
private:
	class Base  // 他用的基类身份
	{
	public:
		// 如果基类的派生类在堆上创建，delete基类指针后 派生类无法调用析构
		virtual ~Base() = default;
	};

	// 派生类
	template<typename T>
	class Derive : public Base
	{
	public:
		Derive(T data):data_(data)
		{}
		T data_;
	};
private:
	// 基类指针 指向 派生类
	std::unique_ptr<Base> base_;  // 基类指针类型的成员变量
};

// 实现一个信号量类semaphore
class Semaphore
{
public:
	Semaphore(int limit=0):resLimit_(limit)
	{}
	~Semaphore() = default;

	// 获取一个信号量资源
	void wait()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		cond_.wait(lock, [&]() {return resLimit_ > 0; });
		resLimit_--;
	}
	// 增加一个信号量资源
	void post()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		resLimit_++;
		cond_.notify_all();
	}
private:
	int resLimit_;
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;
//返回结果
//arg...s
class Result
{
public:
	Result(std::shared_ptr<Task> task, bool isValid = true);
	~Result() = default;

	// 1. 如何获取任务执行完的返回值
	void setVal(AnyType any);
	// 2.get方法，用户调用这个方法获取task返回值
	AnyType get();
private:
	AnyType any_;   // 存储任务的返回值
	Semaphore sem_; // 信号量
	std::shared_ptr<Task> task_;  // 智能指针指向task 不要提前析构了
	std::atomic_bool isValid_;
};

// 任务抽象基类
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result *res);
    // 用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
	virtual AnyType run() = 0;
private:
	Result* result_;  // 千万不能用shared_ptr 造成循环引用  强智能指针互相引用问题！
};

// 线程类
class Thread
{
public:
	// 线程函数对象类型
	using ThreadFunc = std::function<void(size_t)>;
	
	Thread(ThreadFunc func);
	~Thread();
	void start();
	size_t getThreadId();

private:
	ThreadFunc func_;  // 存一个函数对象 传进来一个可调用对象包装器，存起来
	static size_t threadsCount;
	size_t threadsKey;
};
// 线程池支持的两种模式
// enum class 访问枚举型加上作用域
enum class PoolMode
{
	MODE_FIXED,  // 固定容量
	MODE_CACHED, // 可调节容量
};

/*
example:
ThreadPool pool;
pool.start(4);
class MyTask:public Task
{
public:
	MyTask() //构造函数初始化参数 
	{};  
	void run() override
	{//线程代码...}
private:
	parameter
}
pool.submitTask(std::mak_shared<MyTask>());
*/

// 线程类型
class ThreadPool
{
public:
	// 线程池构造
	ThreadPool();
	// 线程池析构
	~ThreadPool();
	//设置工作模式
	void setMode(PoolMode mode);
	// 设置任务上线阈值
	void setTaskQueMaxThreshHold(int threadhold);
	
	void setThreadSizeThreshHold(int threadSize);
	// 提交任务
	Result submitTask(std::shared_ptr<Task> sp);
	// 严阵以待
	void start(int initThreshSize=std::thread::hardware_concurrency()); // 当前系统CPU核心数量

	// 不允许拷贝赋值
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

private:
	// 定义线程函数
	void threadFunc(size_t threadId);

	//检测poo的运行状态
	bool checkRunningState() const;
private:
	std::vector<std::unique_ptr<Thread>> threads_; //线程列表 裸指针需要手动析构 用智能指针 容器析构，元素析构 智能指针删除

	std::unordered_map< size_t, std::unique_ptr<Thread> > threadsMap_; // map下的Thread

	int initThreadSize_;   //初始的线程数量
	std::atomic_int idelThreadSize_; //空闲线程数量
	std::atomic_int curTthreadSize_;     // 记录当前线程数量
	//不能裸指针 要保留用户传入的 完成后自动释放

	//std::queue<Task*>    //需要发生多态 故用指针，但是用户可能临时任务，拿一个析构的对象？？
	std::queue<std::shared_ptr<Task>> taskQue_;//任务队列
	std::atomic_int taskSize_;  //任务的数量 原子类型
	

	int threadSizeThreshHold_; // 线程数量上线阈值
	int taskQueMaxThreshHold_;    //任务阈值
	//同时操作任务队列 
	std::mutex taskQueMxt_;    //保证任务队列的线程安全
	//两个条件变量 notFull notEmpty
	std::condition_variable notFull_;  //任务队列不满
	std::condition_variable notEmpty_; //任务队列不空
	std::condition_variable exitCond_; //等待资源回收

	PoolMode PoolMode_;  //工作模式

	// 当前线程池的启动状态
	std::atomic_bool isPoolRunning_;
	static int minThreadSize_;
};
#endif