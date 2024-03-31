# 基于C++的跨平台线程池

采用C++11完成线程池，支持**fixed**和**cached**两种模式，可提交**任意参数**的任务，并**接收任意返回值**
- 使用unordered_map和queue容器管理线程对象和任务
- 基于条件变量condition_variable和互斥锁mutex实现任务提交线程和任务执行线程间的通信机制
- 采用多态实现线程池submitTask接口，支持任意参数的传递（可以用可变参模板编程和引用折叠原理替代）
- 封装实现Any类、Result类、semaphore，从而接受任意返回值 (可以用future类型替代，减少代码量)

## 工具
VSCode2022开发，linux下g++编译动态库.so，gdb调试

## 如何编译动态库
``` python
g++ -fPIC -shared -lpthread -o libethreadpool.so -std=c++17
```

## Bug修复
在windows和ubuntu下正常运行，在centos7平台下出现死锁。通过gdb attach到对应的死锁进程，查看线程信息 info threads;
查看每个线程 threads 1,各个线程的调用堆栈信息tb ，发现在semphore类中，cond_.nofity_all（）之后在等待。
**分析**：Result出现作用域后，semaphore被析构，但是之后任务完成调用了post()方法，导致死锁。
**解决**：在semaphore类中设置一个标志位，析构后不进行wait()和post(),成功解决


## 使用示例
- 1.实例化threadpool对象
- 2.设置工作模式：fixed和cacehd，默认为fixed
- 3.开启线程池，可指定初始线程数量，默认根据CPU核心数指定
- 4.继承Task任务类，并重写run方法，返回类型为AnyType
- 5.submitTask提交任务，注意需要用智能指针封装
- 6.接受Resul类型返回值
- 7.调用.get()等待返回值，.cast_<T>()拿到返回值
- 8.结束线程池，注意线程池结束时会等待所有task执行完成，才会析构所有线程。

任务task
```c
# 传入参数begin和end，重写run方法，计算累计值，返回结果
class MyTask :public Task
{
public:
	MyTask(int begin,int end)
		:begin_(begin)
		,end_(end)
	{}

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
```
主程序
```c
#include "threadpool.h"

ThreadPool pool;
pool.setMode(PoolMode::MODE_CACHED);
pool.start(2);
pool.submitTask(std::make_shared<MyTask>(1, 2000000));
```
接受返回值：
```c
Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 200000));
uLong sum1 = res1.get().cast_<uLong>();
```

## 代码优化
减少代码量和提交任务的复杂度，采用可变惨模板编程、引用折叠和future类型，优化代码.
优化后，使用可变惨模板编程、package_task、future实现的线程池：[可变惨模板的线程池](https://github.com/ttkingzz/threadpool_future.git)

## contact
For more information or having any question, feel free to contact me.
**WeChat:**
wtxw_zt


