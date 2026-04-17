# dts

https://docs.zephyrproject.org/latest/build/dts/howtos.html
dts本质上是一个配置文件，通过宏函数访问

编译后build目录下的zephyr.dts是所有dts合并后的内容
devicetree_generated.h是处理生成的头文件
## syntax
https://docs.zephyrproject.org/latest/build/dts/intro-syntax-structure.html
## demo
~~~dts
/ {
        soc {
                serial0: serial@40002000 {
                        status = "okay";
                        current-speed = <115200>;
                        /* ... */
                };
        };

        aliases {
                my-serial = &serial0;
        };

        chosen {
                zephyr,console = &serial0;
        };
};
~~~

## 如何访问设备树
https://docs.zephyrproject.org/latest/build/dts/api-usage.html

### 应用层
/home/firebot/zephyrproject/zephyr/include/zephyr/devicetree.h
~~~c
// 第一步获取node identifier,实际是一个字符串
/* Option 1: by node label */
#define MY_SERIAL DT_NODELABEL(serial0)

/* Option 2: by alias */
#define MY_SERIAL DT_ALIAS(my_serial)

/* Option 3: by chosen node */
#define MY_SERIAL DT_CHOSEN(zephyr_console)

/* Option 4: by path */
#define MY_SERIAL DT_PATH(soc, serial_40002000)

// 第二步，获取 宏函数是编译时可用，函数是运行时可用
const struct device *const uart_dev = DEVICE_DT_GET(MY_SERIAL);
const char *dev_name = /* TODO: insert device name from user */;
const struct device *uart_dev = device_get_binding(dev_name);

// 第三步 应用层使用，不同模块的应用层句柄不一样，具体参考sample
if (!device_is_ready(uart_dev)) {
        /* Not ready, do not use */
        return -ENODEV;
}

// 代码中检测驱动状态
#define MY_SERIAL DT_NODELABEL(my_serial)

#if DT_NODE_HAS_STATUS(MY_SERIAL, okay)
const struct device *const uart_dev = DEVICE_DT_GET(MY_SERIAL);
#else
#error "Node is disabled"
#endif
~~~
### 驱动层
为所有status = "okay"的node创建struct device，可以根据实例编号或label创建
都需要将label作为name，node的属性作为驱动配置项
#### 通过实例编号创建 当前推荐方式
同一个compatible有多个实例，从0开始递增，但是具体哪个数字对应哪个node在编译前是不确定的
~~~dts
serial1: serial@40001000 {
        compatible = "vnd,soc-serial";
        status = "disabled";
        current-speed = <9600>;
        ...
};
 
serial2: serial@40002000 {
        compatible = "vnd,soc-serial";
        status = "okay";
        current-speed = <57600>;
        ...
};
~~~

##### 访问设备树属性
/home/firebot/zephyrproject/zephyr/include/zephyr/devicetree.h

通过实例号访问node的属性
~~~c
#define DT_DRV_COMPAT vnd_soc_serial     //必须先创建DT_DRV_COMPAT
DT_DRV_INST(0)                  // node identifier for serial@40001000
DT_INST_PROP(0, current_speed)  // 115200
DT_PROP(DT_DRV_INST(0), current_speed)   //同上
~~~

##### 创建device
~~~c
/*
 * This instantiation macro is named "CREATE_MY_DEVICE".
 * Its "inst" argument is an arbitrary instance number.
 *
 * Put this near the end of the file, e.g. after defining "my_api_funcs".
 */
#define CREATE_MY_DEVICE(inst)                                       \
     static struct my_dev_data my_data_##inst = {                    \
             /* initialize RAM values as needed, e.g.: */            \
             .freq = DT_INST_PROP(inst, clock_frequency),            \
     };                                                              \
     static const struct my_dev_cfg my_cfg_##inst = {                \
             /* initialize ROM values as needed. */                  \
     };                                                              \
     DEVICE_DT_INST_DEFINE(inst,                                     \
                           my_dev_init_function,                     \
                           NULL,                                     \
                           &my_data_##inst,                          \
                           &my_cfg_##inst,                           \
                           MY_DEV_INIT_LEVEL, MY_DEV_INIT_PRIORITY,  \
                           &my_api_funcs);
/* Call the device creation macro for each instance: */
DT_INST_FOREACH_STATUS_OKAY(CREATE_MY_DEVICE)
~~~

#### 通过node label创建
这里有两个label：mydevice0，mydevice1
~~~dts
/ {
        soc {
                mydevice0: dev@0 {
                        compatible = "vnd,my-device";
                };
                mydevice1: dev@1 {
                        compatible = "vnd,my-device";
                };
        };
};
~~~

##### 访问设备树属性和创建device
~~~c
#define MYDEV(idx) DT_NODELABEL(mydevice ## idx)  //创建node identifier

/*
 * Define your instantiation macro; "idx" is a number like 0 for mydevice0
 * or 1 for mydevice1. It uses MYDEV() to create the node label from the
 * index.
 */
#define CREATE_MY_DEVICE(idx)                                        \
     static struct my_dev_data my_data_##idx = {                     \
             /* initialize RAM values as needed, e.g.: */            \
             .freq = DT_PROP(MYDEV(idx), clock_frequency),           \
     };                                                              \
     static const struct my_dev_cfg my_cfg_##idx = { /* ... */ };    \
     DEVICE_DT_DEFINE(MYDEV(idx),                                    \
                     my_dev_init_function,                           \
                     NULL,                                           \
                     &my_data_##idx,                                 \
                     &my_cfg_##idx,                                  \
                     MY_DEV_INIT_LEVEL, MY_DEV_INIT_PRIORITY,        \
                     &my_api_funcs)

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mydevice0), okay)  //访问node的属性
CREATE_MY_DEVICE(0)     //手动依次创建device
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(mydevice1), okay)
CREATE_MY_DEVICE(1)
#endif
~~~


#### 设备驱动模型

https://docs.zephyrproject.org/latest/kernel/drivers/index.html

为什么不和linux一样，都通过read/write访问/dev/xxx？nuttx就是这样
- 效率不好，mcu更看重性能
- 驱动的控制和字节流模型不匹配，仍然需要很多ioctl函数导致每种硬件都不能通用api

驱动层api：/home/firebot/zephyrproject/zephyr/include/zephyr/device.h
约定：对应用层提供的api是同步的
通过Kconfig 关闭没用的驱动以节省固件大小
通过__syscall 定义无法通过通用api定义的函数
~~~c
/* my_driver.c */
#include <zephyr/drivers/some_api.h>

struct device {
      const char *name;
      const void *config;   //配置，只读
      const void *api;      //api，每种驱动不一样
      void * const data;    //运行过程中的数据
};

/* Define data (RAM) and configuration (ROM) structures: */
struct my_dev_data {
     /* per-device values to store in RAM */
};
struct my_dev_cfg {
     uint32_t freq; /* Just an example: initial clock frequency in Hz */
     /* other configuration to store in ROM */
};

/* Implement driver API functions (drivers/some_api.h callbacks): */
static int my_driver_api_func1(const struct device *dev, uint32_t *foo) { /* ... */ }
static int my_driver_api_func2(const struct device *dev, uint64_t bar) { /* ... */ }
static struct some_api my_api_funcs = {
     .func1 = my_driver_api_func1,
     .func2 = my_driver_api_func2,
};
//通过DEVICE_DEFINE定义device并指定初始化顺序
//west build -t initlevels 查看驱动的实际初始化顺序
//通过SYS_INIT初始化阶段自动调用的函数
//通过DEVICE_MMIO_XXX处理运行时才能获取的地址，比如硬件地址经过mmu转换后的虚拟地址
~~~
## overlays
看文档，太啰嗦

## 验证

# dts binding
也处理到了devicetree_generated.h
## 验证
~~~c
/*
 * Devicetree node: /soc/flash-controller@40010400
 *
 * Node identifier: DT_N_S_soc_S_flash_controller_40010400
 *
 * Binding (compatible = hdsc,hc32-flash-controller):
 *   /home/firebot/zephyrproject/zephyr-example/hc32f460petb/dts/bindings/flash/hdsc,hc32-flash-controller.yaml
 *
 * (Descriptions have moved to the Devicetree Bindings Index
 * in the documentation.)
 */

~~~

# gpio子系统

## 驱动分析 hk32-gpio

## 驱动分析 stm32-gpio
