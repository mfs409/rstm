#ifndef LIBSTM_SANDBOXING_TIMER_H
#define LIBSTM_SANDBOXING_TIMER_H

#include <csignal>

namespace stm {
namespace sandbox {
bool demultiplex_timer(int, siginfo_t*, void*);
}
}

#endif // LIBSTM_SANDBOXING_TIMER_H
