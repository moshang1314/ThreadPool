#include "threadpool.h"
#include <thread>
#include <functional>
#include <iostream>

const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //单位：s

//线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_( 0 )
    , taskSize_( 0 )
    , idleThreadSize_( 0 )
    , curThreadSize_(0)
    , taskQueMaxThreshHold_( TASK_MAX_THRESHHOLD )
    , threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
    , poolMode_( PoolMode::MODE_FIXED )
    , isPoolRunning_(false) {}

//线程池析构
ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;
    //等待线程池里面所有的线程返回 有两种状态：阻塞 & 正在执行任务，还有注意一种隐藏的状态，线程正在获取锁的状态，此时notify_all()并不能唤醒该线程，会导致死锁
    std::unique_lock<std::mutex> lock( taskQueMtx_ );
    notEmpty_.notify_all();
    exitCond_.wait( lock , [ & ] ()->bool {return curThreadSize_ == 0;} );
}

//设置线程池的工作模式
void ThreadPool::setMode( PoolMode mode ) {
    if (checkRunningState()) {
        return;
    }
    poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold( int threshhold ) {
    if (checkRunningState())
        return;
     taskQueMaxThreshHold_ = threshhold;
}

// 设置线程池cached模式下的线程数阈值
void ThreadPool::setThreadSizeThreshHold( int threshhold ) {
    if (checkRunningState())
        return;
    if (poolMode_ == PoolMode::MODE_CACHED) {
        threadSizeThreshHold_ = threshhold;
    }
}

//给线程池提交任务，用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask( std::shared_ptr<Task> sp ) {
    //获取锁
    std::unique_lock<std::mutex> lock( taskQueMtx_ );
    
    //线程通信 等待任务队列有空余
    // 用户提交任务，最长不能阻塞超过1s，否则判断提交失败，返回
    if (!notFull_.wait_for( lock , std::chrono::seconds( 1 ) , [ & ] ()->bool {return taskQue_.size() < (size_t)taskQueMaxThreshHold_;} )) {
        //表示条件变量等待超时
        std::cerr << "task queue is full, submit task fail." << std::endl;
        // return task->getResult(); // 线程执行完task，task对象就被析构掉了
        return Result( sp , false );
    }

    //如果有空余，把任务放入任务队列中
    taskQue_.emplace( sp );
    taskSize_++;
    
    //因为放了新任务，任务队列肯定不空了，notEmpty_通知
    notEmpty_.notify_all();

    // cache模式
    if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_) {
        //创建新线程
        auto ptr = std::make_unique<Thread>( std::bind( &ThreadPool::threadFunc , this , std::placeholders::_1) );
        int threadId = ptr->getId();
        threads_.emplace( threadId , std::move( ptr ) );
        threads_[threadId]->start();
        curThreadSize_++;
        idleThreadSize_++;
        std::cout << "create new thread...." << std::endl;
    }

    //返回任务的Result对象
    return Result( sp );
}

//开启线程池
void ThreadPool::start( int initThreadSize ) { 
    //设置线程池的启动状态
    isPoolRunning_ = true;
    //记录初始线程个数
    initThreadSize_ = initThreadSize;

    //创建线程个数
    for (int i = 0; i < initThreadSize_; i++) {
        auto ptr = std::make_unique<Thread>( std::bind( &ThreadPool::threadFunc , this , std::placeholders::_1) );
        int threadId = ptr->getId();
        threads_.emplace( threadId , std::move(ptr));
        //threads_.emplace_back( std::move( ptr ) );
    }
 
    //启动所有线程
    for (int i = 0; i < initThreadSize_; i++) {
        threads_[i]->start();
        idleThreadSize_++; //记录初始空闲线程的数量
        curThreadSize_++;
    }
}

//线程函数，线程池的所有线程从任务队列中消费任务
void ThreadPool::threadFunc(int threadId) {
    auto lastTime = std::chrono::high_resolution_clock().now();
    for(;;) {
        std::shared_ptr<Task> task;
        {
            //先获取锁
            std::unique_lock<std::mutex> lock( taskQueMtx_ );

            std::cout << "tid: " << std::this_thread::get_id()
                << "尝试获取任务..." << std::endl;
            while (taskQue_.size() == 0) {
                //线程池要结束，回收线程资源
                if (!isPoolRunning_) {
                    threads_.erase( threadId );
                    curThreadSize_--;
                    exitCond_.notify_all();
                    std::cout << "thread: " << std::this_thread::get_id() << " exit..." << std::endl;
                    return;
                }
                //cache模式下，有可能创建了很多线程，但是空闲时间超过60s，应该把多余的线程回收掉
                if (poolMode_ == PoolMode::MODE_CACHED) {
                     if (std::cv_status::timeout == notEmpty_.wait_for( lock , std::chrono::seconds( 1 ) )) {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>( now - lastTime );
                        if (dur.count() >= 60 && curThreadSize_ > initThreadSize_) {
                            //开始回收线程
                            //记录线程数量的相关变量的值修改
                            //把  threadFunc <-> Thread对象
                            threads_.erase( threadId );
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout << "thread: " << std::this_thread::get_id() << " exit..." << std::endl;
                            return;
                        }
                     }
                }
                else {
                    //等待notEmpty条件
                    notEmpty_.wait( lock);
                }
                // // 线程池要结束，回收线程资源
                // if (!isPoolRunning_) {
                //     threads_.erase( threadId );
                //     curThreadSize_--;
                //     exitCond_.notify_all();
                //     std::cout << "thread: " << std::this_thread::get_id() << " exit..." << std::endl;
                //     return;
                // }
            }
            idleThreadSize_--;
            //从任务队列中取出一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;
            
            std::cout << "tid: " << std::this_thread::get_id()
                << "获取任务成功..." << std::endl;
            
            //如果依然还有剩余任务，继续通知其他线程执行任务
            if (taskQue_.size() > 0) {
                notEmpty_.notify_all();
            }
            //取出一个任务，进行通知
            notFull_.notify_all();
        }
        //当前线程负责执行这个任务
        if (task != nullptr) {
            task->exec();
        }
        idleThreadSize_++;
        lastTime = std::chrono::high_resolution_clock().now();  //更新线程执行完任务的时间
    }
}

bool ThreadPool::checkRunningState()const {
    return isPoolRunning_;
}
////////////////线程方法实现
int Thread::generateId_ = 0;

//线程构造
Thread::Thread( ThreadFunc func )
    : func_(func)
    , threadId_(generateId_++) {}

//线程析构 
Thread::~Thread() {}

//启动线程
void Thread::start() {
    //创建一个线程来执行一个线程函数
    std::thread t( func_ , threadId_);
    t.detach(); //设置分离线程
}

// 获取线程id
int Thread::getId() const {
    return threadId_;
}
////////////////////////// Task
Task::Task() : result_( nullptr ) {}

void Task::exec() {
    if (result_ != nullptr) {
        result_->setVal( run() );
    }
} 

void Task::setResult( Result* res ) {
    result_ = res;
}
 
////////////////////////    Result
Result::Result( std::shared_ptr<Task> task , bool isValid )
    : isValid_( isValid )
    , task_( task ) {
    task_->setResult( this );
}


Any Result::get() {
    if (!isValid_) {
        return "";
    }
    sem_.wait();    // task任务如果没有执行完，这里会阻塞用户的线程
    return std::move( any_ );
}

void Result::setVal(Any any) {
    //存储task的返回值
    this->any_ = std::move( any );
    sem_.post();    //已经获取了任务的返回值，增加信号量资源
}

Result::Result( const Result& result )
    : isValid_( result.isValid_.load() )
    , task_( result.task_ ) {
    task_->setResult( this );
}