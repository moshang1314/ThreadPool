#include <iostream>
#include "threadpool.h"
#include <chrono>
#include <thread>

using uLong = unsigned long long;
class MyTask : public Task {
public: 
    MyTask( int begin , int end )
        : begin_( begin )
        , end_( end ) {}
    
    Any run() {
        uLong sum = 0;
        for (int i = begin_; i <= end_; i++)
            sum += i;
        return sum;
    }
private:
    int begin_;
    int end_;
};

int main() {
    ThreadPool pool;
    pool.setMode( PoolMode::MODE_CACHED );
    pool.start( 2 );
    Result res1 = pool.submitTask( std::make_shared<MyTask>( 1 , 10000000 ) );
    Result res2 = pool.submitTask( std::make_shared<MyTask>( 10000001 , 20000000 ) );
    Result res3 = pool.submitTask( std::make_shared<MyTask>( 20000001 , 30000000 ) );

    uLong sum1 = res1.get().cast_<uLong>();
    uLong sum2 = res2.get().cast_<uLong>();
    uLong sum3 = res3.get().cast_<uLong>();

    //存在的问题，当主线程没有调用Result的get函数等待任务执行完，则当出main函数时，Result对象先被析构
    //其内部存在的信号量也会被析构，而任务完成后会调用其绑定的Result对象的信号量的notify_all函数，这时会产生问题
    //对一个已经释放了的条件变量调用notify_all会导致死锁问题，因此应该调用wait和notify_all函数时，应该判断该变量是否已经被析构
    std::cout << ( sum1 + sum2 + sum3 ) << std::endl;
    getchar();
    return 0;
}