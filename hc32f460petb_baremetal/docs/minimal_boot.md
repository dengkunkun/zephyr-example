# 最小启动文件
HC32F460xE.ld 链接脚本
startup_hc32f460petb.S 启动汇编文件	
hc32f460petb.h/hc32f460.h   所有的寄存器结构体地址、中断id
system_hc32f460petb.h/c或system_hc32f460.h/c 
- SystemInit 使能fpu，初始化时钟，设置中断向量表
- SystemCoreClockUpdate  初始化时钟
hc32f4xx_conf.h  各个驱动模块的使能配置
main.c  默认情况下为空

复杂度：低