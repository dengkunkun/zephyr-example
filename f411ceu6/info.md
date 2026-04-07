资料：
[WeActStudio/WeActStudio.MiniSTM32F4x1: MiniF4-STM32F401CEU6/STM32F411CEU6 Product Literature](https://github.com/WeActStudio/WeActStudio.MiniSTM32F4x1)

编译：
west build -b  blackpill_f411ce ./blinky 2>&1|tee build.log    

烧写：
需要按住boot按键
~~~sh
 /home/firebot/zephyr-sdk-1.0.1/hosttools/sysroots/x86_64-pokysdk-linux/usr/bin/openocd \                            х INT Py zephyrproject
  -f interface/cmsis-dap.cfg \
  -f target/stm32f4x.cfg \
  -c "transport select swd" \
  -c "adapter speed 4000" \
  -c "program build/zephyr/zephyr.hex verify reset exit"
~~~

在添加定制board后，可以直接west flash和west debug