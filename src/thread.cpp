// Copyright (c) 2021 The Bitcoin Core developers
// Copyright (c) 2022 The Raptoreum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php

#include <thread.h>

#include <logging.h>
#include <util.h>
#include <threadnames.h>

#include <exception>

void util::TraceThread(const std::string thread_name, std::function<void()> thread_func)
{
  std::string name_str = "rtm-" + thread_name;
  util::ThreadRename(name_str.c_str());
  try {
    LogPrintf("%s thread start\n", thread_name);
    thread_func();
    LogPrintf("%s thread exit\n", thread_name);
  }
  catch (const std::exception& e) {
    PrintExceptionContinue(std::current_exception(), thread_name.c_str());
    throw;
  } catch (...) {
    PrintExceptionContinue(std::current_exception(), thread_name.c_str());
    throw;
  }
}