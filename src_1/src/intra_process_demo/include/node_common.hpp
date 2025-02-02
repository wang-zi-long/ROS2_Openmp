﻿#ifndef DEMOS__NODE_COMMON_H
#define DEMOS__NODE_COMMON_H

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <thread>
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <stdlib.h>
#include "std_msgs/msg/string.hpp"
#include "/usr/local/lib/gcc/x86_64-pc-linux-gnu/15.0.0/include/omp.h"
#include "conf.hpp"

#define DUMMY_LOAD_ITER	      100

uint64_t dummy_load_calib = 1;

uint64_t get_clocktime() { 
    long int        ns; 
    uint64_t        all; 
    time_t          sec; 
    struct timespec spec; 

    clock_gettime(CLOCK_REALTIME, &spec);

    sec   = spec.tv_sec; 
    ns    = spec.tv_nsec; 
    all   = (uint64_t) sec * 1000000000UL + (uint64_t) ns; 
    return all;  
}

void dummy_load_ms(int load_ms) {
  volatile uint64_t i, j;
  for (j = 0; j < dummy_load_calib * load_ms; j++)
      for (i = 0 ; i < DUMMY_LOAD_ITER; i++) 
          __asm__ volatile ("nop");
}

void dummy_load_100us(int load_100us, bool use_openmp) {
  volatile uint64_t i, j;
  if(use_openmp){
    #pragma omp parallel private(i) num_threads(OPENMP_THREAD_NUM)
    {
      volatile uint64_t start = get_clocktime();
      long int tid = syscall(SYS_gettid);

      #pragma omp for
      for (j = 0; j < (dummy_load_calib * load_100us /10); j++)
        for (i = 0 ; i < DUMMY_LOAD_ITER; i++) 
            __asm__ volatile ("nop");

      volatile uint64_t end = get_clocktime();
      printf("|TID:%ld|-->[Start@%lu-End@%lu-Cost:%lu(ns)]\n", tid, start, end, end - start);
    }
  }else{
    for (j = 0; j < (dummy_load_calib * load_100us /10); j++)
      for (i = 0 ; i < DUMMY_LOAD_ITER; i++) 
          __asm__ volatile ("nop");
  }
}

void dummy_load_100us_new(int load_100us, bool use_openmp, int chain_idx, int node_idx, uint32_t data, rclcpp::executors::MultiThreadedExecutor &executor) {
  if(use_openmp){
    uint64_t start1, end1;
    // 执行到此处的执行器线程，只能成为omp主线程
    // @Note@：当存在多条任务链时，另一个执行器线程可能正在执行就绪回调，是否需要监控一下另一个执行器线程的执行状态？？？
    #pragma omp parallel for private(start1, end1) num_threads(OPENMP_THREAD_NUM)
    for (uint64_t j = 0; j < (dummy_load_calib * load_100us / 10); j++){

        // if(j == 0){
        //     start1 = get_clocktime();
        // }
        
        for (int i = 0; i < DUMMY_LOAD_ITER; i++) {
            __asm__ volatile ("nop");
        }

        // if(j == (((dummy_load_calib * load_100us / 10) - 1 ))){
        //   end1 = get_clocktime();
        //   long int Tid = syscall(SYS_gettid);
        //   int cpu_core = sched_getcpu();
        //   printf("|TID:%ld|-->[Chain%u-Node%u.exe:%u(100us).on:%d]-->[Start@%lu-End@%lu-Cost:%lu(ns) and Published msg: %d]\n",
        //            Tid, chain_idx, node_idx, load_100us, cpu_core, start1, end1, end1 - start1, data);
        // }
    }
    executor.trigger_interrupt_guard();
    dequeue3();
    // long int Tid = syscall(SYS_gettid);
    // int cpu_core = sched_getcpu();
    // uint64_t current_time  = get_clocktime();
    // printf("|TID:%ld|-->[Chain%u-Node%u.exe:%u(100us).on:%d]-->[After parallel:%lu and Published msg: %d]\n",
    //             Tid, chain_idx, node_idx, load_100us, cpu_core, current_time, data);
  }else{
    uint64_t i,j;
    for (j = 0; j < (dummy_load_calib * load_100us /10); j++)
      for (i = 0 ; i < DUMMY_LOAD_ITER; i++) 
          __asm__ volatile ("nop");
  }
}

void dummy_load_calibration() {
  volatile uint64_t ts_start, ts_end;
  while(1) {
    ts_start = get_clocktime(); // in ns
    dummy_load_ms(100);         // 100ms
    ts_end = get_clocktime();   // in ns
    int duration_ns = ts_end - ts_start;
    if (abs(duration_ns - 100*1000*1000) < 1000000) {// error margin: 1ms
      break;
    }
    dummy_load_calib = 100*1000*1000*dummy_load_calib / duration_ns;
    if (dummy_load_calib <= 0) {
      dummy_load_calib = 1;
    }
  }
  ts_start = get_clocktime(); // in ns
  dummy_load_ms(10);          // 10ms
  ts_end = get_clocktime();   // in ns
  printf("|CALIBRATION TEST|[Setting: 10ms]->@time-measure: %lu. \r\n", ts_end-ts_start);
}

int cnter_val[5] = {0, 0, 0, 0, 0};

struct Sensor : public rclcpp::Node {
  Sensor(const std::string & node_name, const std::string & output_topic, uint32_t chain_idx, uint32_t node_idx, uint32_t exe_time_100us, std::chrono::duration<int,std::milli> period_ms, bool use_openmp, int priority, rclcpp::executors::MultiThreadedExecutor &executor)
  : Node(node_name, rclcpp::NodeOptions().use_intra_process_comms(false)) {
    pub_ = this->create_publisher<std_msgs::msg::Int32>(output_topic, 10);
    std::weak_ptr<std::remove_pointer<decltype(pub_.get())>::type> captured_pub = pub_;
    auto callback = [this, captured_pub, node_idx, chain_idx, exe_time_100us, use_openmp, priority, &executor]() -> void {
        long int tid = syscall(SYS_gettid);
        // if(priority != 0){
        //   // 修改执行器线程以及OpenMP线程的优先级
        //   if(use_openmp){
        //     #pragma omp parallel num_threads(OPENMP_THREAD_NUM)
        //     {
        //       std::string command = "sudo chrt -f -p " + std::to_string(priority) + " " + std::to_string(tid);
        //       system(command.c_str());
        //     }
        //   }else{
        //     std::string command = "sudo chrt -f -p " + std::to_string(priority) + " " + std::to_string(tid);
        //     system(command.c_str());
        //   }
        // }
        auto pub_ptr = captured_pub.lock();
        if (!pub_ptr) {
          return;
        }
        // std::thread::id thread_id = std::this_thread::get_id();
        std_msgs::msg::Int32::UniquePtr msg(new std_msgs::msg::Int32());
        msg->data = cnter_val[chain_idx-1]++;

        volatile uint64_t ts_start = get_clocktime();
        // dummy_load_100us(exe_time_100us, use_openmp);
        dummy_load_100us_new(exe_time_100us, use_openmp, chain_idx, node_idx, msg->data, executor);
        volatile uint64_t ts_end = get_clocktime();
        int cpu_core = sched_getcpu();

        printf("|TID:%ld|-->[Chain%u-Sensor%u.exe:%u(100us).on:%d]-->[Start@%lu-End@%lu-Cost:%lu(ns) and Published msg: %d]. \r\n\n", 
                tid,
                chain_idx,
                node_idx,
                exe_time_100us,
                cpu_core,
                ts_start,
                ts_end,
                ts_end-ts_start,
                msg->data
              );

        pub_ptr->publish(std::move(msg));

        // if(priority != 0){
        //   // 恢复执行器线程的优先级
        //   std::string command = "sudo chrt -f -p 50 " + std::to_string(tid);
        //   system(command.c_str());
        // }

      };
    timer_ = this->create_wall_timer(period_ms, callback);
  }
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

/// MARK: Transfer Node structure definition.
struct Transfer : public rclcpp::Node {
  Transfer(const std::string & node_name, const std::string & input_topic, const std::string & output_topic, uint32_t chain_idx, uint32_t node_idx, uint32_t exe_time_100us, bool use_openmp, int priority, rclcpp::executors::MultiThreadedExecutor &executor)
  : Node(node_name, rclcpp::NodeOptions().use_intra_process_comms(false)) {
    pub_ = this->create_publisher<std_msgs::msg::Int32>(output_topic, 10);
    std::weak_ptr<std::remove_pointer<decltype(pub_.get())>::type> captured_pub = pub_;
    sub_ = this->create_subscription<std_msgs::msg::Int32>(
      input_topic,
      10,
      [this, captured_pub, node_idx, chain_idx, exe_time_100us, use_openmp, priority, &executor](std_msgs::msg::Int32::UniquePtr msg) {
          
          long int tid = syscall(SYS_gettid);

          // if(priority != 0){
          //   // 修改执行器线程以及OpenMP线程的优先级
          //   if(use_openmp){
          //     #pragma omp parallel num_threads(OPENMP_THREAD_NUM)
          //     {
          //       std::string command = "sudo chrt -f -p " + std::to_string(priority) + " " + std::to_string(tid);
          //       system(command.c_str());
          //     }
          //   }else{
          //     std::string command = "sudo chrt -f -p " + std::to_string(priority) + " " + std::to_string(tid);
          //     system(command.c_str());
          //   }
          // }

          volatile uint64_t ts_start = get_clocktime();
          auto pub_ptr = captured_pub.lock();
          if (!pub_ptr) {
            return;
          }

          // std::thread::id thread_id = std::this_thread::get_id();
          // dummy_load_100us(exe_time_100us, use_openmp);
          // dummy_load_100us_new(exe_time_100us, use_openmp, chain_idx, node_idx, msg->data);
          dummy_load_100us_new(exe_time_100us, use_openmp, chain_idx, node_idx, msg->data, executor);
          volatile uint64_t ts_end = get_clocktime();
          int cpu_core = sched_getcpu();

          printf("|TID:%ld|-->[Chain%u-Transfer%u.exe:%u(100us).on:%d]-->[Start@%lu-End@%lu-Cost:%lu(ns) and Route msg: %d]. \r\n\n", 
                  tid,
                  chain_idx,
                  node_idx,
                  exe_time_100us,
                  cpu_core,
                  ts_start,
                  ts_end,
                  ts_end-ts_start,
                  msg->data
                );
          pub_ptr->publish(std::move(msg));

          // if(priority != 0){
          //   std::string command = "sudo chrt -f -p 50 " + std::to_string(tid);
          //   system(command.c_str());
          // }

      }
    );
  }
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr sub_;
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr pub_;
};

/// MARK: Command Node structure definition.
struct Command : public rclcpp::Node {
  Command(const std::string & node_name, const std::string & input_topic, uint32_t chain_idx, uint32_t node_idx, uint32_t exe_time_100us, bool use_openmp, int priority, rclcpp::executors::MultiThreadedExecutor &executor)
  : Node(node_name, rclcpp::NodeOptions().use_intra_process_comms(false)) {
    sub_ = this->create_subscription<std_msgs::msg::Int32>(
      input_topic,
      10,
      [this, node_idx, chain_idx, exe_time_100us, use_openmp, priority, &executor](std_msgs::msg::Int32::UniquePtr msg) {

          long int tid = syscall(SYS_gettid);

          // if(priority != 0){
          //   // 修改执行器线程以及OpenMP线程的优先级
          //   if(use_openmp){
          //     #pragma omp parallel num_threads(OPENMP_THREAD_NUM)
          //     {
          //       std::string command = "sudo chrt -f -p " + std::to_string(priority) + " " + std::to_string(tid);
          //       system(command.c_str());
          //     }
          //   }else{
          //     std::string command = "sudo chrt -f -p " + std::to_string(priority) + " " + std::to_string(tid);
          //     system(command.c_str());
          //   }
          // }

          volatile uint64_t ts_start = get_clocktime();
          // std::thread::id thread_id = std::this_thread::get_id();
          // dummy_load_100us(exe_time_100us, use_openmp);
          // dummy_load_100us_new(exe_time_100us, use_openmp, chain_idx, node_idx, msg->data);
          dummy_load_100us_new(exe_time_100us, use_openmp, chain_idx, node_idx, msg->data, executor);
          volatile uint64_t ts_end = get_clocktime();
          int cpu_core = sched_getcpu();

          printf("|TID:%ld|-->[Chain%u-Command%u.exe:%u(100us).on:%d]-->[Start@%lu-End@%lu-Cost:%lu(ns) and Received msg: %d]. \r\n\n", 
                  tid,
                  chain_idx,
                  node_idx,
                  exe_time_100us,
                  cpu_core,
                  ts_start,
                  ts_end,
                  ts_end-ts_start,
                  msg->data
                );

          // if(priority != 0){
          //   std::string command = "sudo chrt -f -p 50 " + std::to_string(tid);
          //   system(command.c_str());
          // }
      }
    );
  }
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr sub_;
};

#endif