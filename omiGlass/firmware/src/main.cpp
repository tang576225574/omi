/**
 * 固件主入口文件
 *
 * 这是Arduino框架的标准入口文件,调用app.h中定义的setup_app()和loop_app()函数
 * 实际的业务逻辑在app.cpp中实现
 */
#include <Arduino.h>
#include "app.h"

/**
 * setup - Arduino初始化函数
 *
 * 在系统启动时调用一次,用于初始化硬件和各个模块
 */
void setup() {
  setup_app();
}

/**
 * loop - Arduino主循环函数
 *
 * 系统启动后持续循环执行,处理各种任务和事件
 */
void loop() {
  loop_app();
}
