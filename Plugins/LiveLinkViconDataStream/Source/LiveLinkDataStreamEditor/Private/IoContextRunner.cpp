#include "IoContextRunner.h"

#include "ViconAsioThrowException.hpp"
#include "HAL/RunnableThread.h"

FIoContextRunner::FIoContextRunner()
  : m_IoContext()
  , m_Work(m_IoContext.get_executor())
{
  m_pThread = FRunnableThread::Create(this, TEXT("IoContextRunner"));
}

FIoContextRunner::~FIoContextRunner()
{
  Stop();
  if (m_pThread != nullptr)
  {
    m_pThread->WaitForCompletion();
    delete m_pThread;
  }
};

bool FIoContextRunner::Init()
{
  return true;
};

uint32 FIoContextRunner::Run()
{
  std::error_code ErrorCode;
  m_IoContext.run(ErrorCode);
  return ErrorCode.value();
};

void FIoContextRunner::Stop()
{
  // We require that all operations and handlers are allowed to finish normally before stopping
  // so that all async operations finish with error codes rather than exceptions, so this is
  // the appropriate idiom. See the asio::io_context documentation for more details
  m_Work.reset();
};
