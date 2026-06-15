#pragma once

#include <exception>
#include <iostream>

namespace asio
{
  namespace detail
  {

// Provide an implementation of asio::detail::throw_exception
// as the default implementation is not provided if `ASIO_NO_EXCEPTIONS` is defined
#if defined(ASIO_NO_EXCEPTIONS)
    template<typename Exception>
    void throw_exception(const Exception& e)
    {
      std::cout << "ASIO Exceptions are disabled and cannot be handled, terminate the program.\n";
      std::terminate();
    }
#endif  // defined(ASIO_NO_EXCEPTIONS)

  }  // namespace detail
}  // namespace asio
