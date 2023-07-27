#include <mutex>
#include <iostream>
#include <atomic>
#include <memory>
using namespace std;
class Test {
public:
    Test() {
        std::cout << "Test()" << std::endl;
    }
    
    // Test& operator=( const Test& ) {
    //     std::cout << "operator=(const Test&)" << std::endl;
    // }
    Test& operator=( const Test&& ) = delete;
    //Test& operator=( Test& ) = default;
    void add() {
        a = a + 2;
    }
private:
    int a;
    //std::unique_ptr<int> b;
};


int main() {
    Test t1;
    Test t;
    t = t1;
}