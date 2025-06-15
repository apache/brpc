#include<iostream>
#include<thread>
using namespace std;



void fun1(){

    cout<<"fun1"<<endl;
//0x7ffff73fee80
//0x7ffff73fee80
//0x7ffff73fee80
}

void fun2(){

    cout<<"fun2"<<endl;
    //0x7ffff6bfde80
    //0x7ffff6bfde80
    //0x7ffff6bfde80
    // fun3();
}

void fun3(){

    cout<<"fun3"<<endl;
    //0x7ffff63fce80
    // int fun3_n=1;
}


int var=1;
int main(int argc, char* argv[]) {
//0x7fffffffe220
// 0x7fffffffe220
    int main_n=1;
    (void)main_n;
    // fun1();
    // fun2();
    thread t1(fun1);
    thread t2(fun2);
    thread t3(fun3);
    t1.join();
    t2.join();
    t3.join();

    // main://0x7fffffffe180
    // t1//0x7ffff73fee80
    // t2//0x7ffff6bfde80
    // t3//0x7ffff63fce80
    return 0;
}


