#include <stdio.h>
#include<stdarg.h>


void log_print(const char *format, ...){
    char my_dat[256]={0};
    char my_buf[200]={0};
    va_list my_ap;  //定义参数指针，获取可选参数
    va_start(my_ap,format);     //初始化参数指针，将ap指向第一个实际参数的地址
    
    vsprintf(my_buf,format,my_ap);
    printf("##**## %s\n",my_buf);

    // std::string ssss(my_buf);

    // std::cout <<"##**## "<<ssss<< std::endl;

    va_end(my_ap);      //不再使用参数指针，或者需要重新初始化参数指针时，必须先调用va_end宏

}