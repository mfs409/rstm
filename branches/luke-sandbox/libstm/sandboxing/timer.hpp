#ifndef LIBSTM_SANDBOXING_TIMER_H
#define LIBSTM_SANDBOXING_TIMER_H

#include <csignal>

namespace stm {
namespace sandbox {

void init_timer_validation();

bool demultiplex_timer(int, siginfo_t*, void*);

}
}

#endif // LIBSTM_SANDBOXING_TIMER_H
