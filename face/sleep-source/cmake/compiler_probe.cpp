#include <iostream>
#if !defined(_M_X64)
#error The configured compiler did not produce an x64 target
#endif
int main(){static_assert(sizeof(void*)==8,"x64 required");std::cout<<"PASS x64 target, MSVC "<<_MSC_VER<<", pointer size "<<sizeof(void*)<<"\n";return 0;}
