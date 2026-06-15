#pragma once

#include "HAL/Runnable.h"
#include "Templates/SharedPointer.h"
#include "asio/executor_work_guard.hpp"
#include "asio/io_context.hpp"

class FRunnableThread;

class LIVELINKDATASTREAMEDITOR_API FIoContextRunner : public FRunnable
{
public:
  // Constructor
  explicit FIoContextRunner();
  // Destructor
  ~FIoContextRunner();
  // Begin FRunnable interface.
  virtual bool Init() override;
  virtual uint32 Run() override;
  virtual void Stop() override;
  // End FRunnable interface

  asio::io_context m_IoContext;

private:
  using TWorkGuard = decltype(asio::make_work_guard(std::declval<asio::io_context&>()));
  FRunnableThread* m_pThread;
  TWorkGuard m_Work;
};
