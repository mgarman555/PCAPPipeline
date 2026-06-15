#pragma once

#include "HAL/Runnable.h"
#include "Templates/SharedPointer.h"

// Asio uses Win32 types/atomics (TRUE/FALSE, Interlocked*) that UE hides by default.
// Wrap the includes the same way this plugin's NSDBrowserWin.h already does, or the
// bundled Asio fails to compile in UE 5.7's environment.
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include "asio/executor_work_guard.hpp"
#include "asio/io_context.hpp"
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

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
