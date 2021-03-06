#include "debug.h"

#include <execinfo.h>
#include <gnu/libc-version.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <cassert>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

#include <glog/logging.h>
#include <rte_config.h>
#include <rte_version.h>

#include "module.h"
#include "snobj.h"
#include "snbuf.h"
#include "tc.h"
#include "utils/htable.h"

#define STACK_DEPTH 64

static const char *si_code_to_str(int sig_num, int si_code) {
  /* See the manpage of sigaction() */
  switch (si_code) {
    case SI_USER:
      return "SI_USER: kill";
    case SI_KERNEL:
      return "SI_KERNEL: sent by the kernel";
    case SI_QUEUE:
      return "SI_QUEUE: sigqueue";
    case SI_TIMER:
      return "SI_TIMER: POSIX timer expired";
    case SI_MESGQ:
      return "SI_MESGQ: POSIX message queue state changed";
    case SI_ASYNCIO:
      return "SI_ASYNCIO: AIO completed";
    case SI_SIGIO:
      return "SI_SIGIO: Queued SIGIO";
    case SI_TKILL:
      return "SI_TKILL: tkill or tgkill";
  }

  switch (sig_num) {
    case SIGILL:
      switch (si_code) {
        case ILL_ILLOPC:
          return "ILL_ILLOPC: illegal opcode";
        case ILL_ILLOPN:
          return "ILL_ILLOPN: illegal operand";
        case ILL_ILLADR:
          return "ILL_ILLADR: illegal addressing mode";
        case ILL_ILLTRP:
          return "ILL_ILLTRP: illegal trap";
        case ILL_PRVOPC:
          return "ILL_PRVOPC: privileged opcode";
        case ILL_PRVREG:
          return "ILL_PRVREG: privileged register";
        case ILL_COPROC:
          return "ILL_COPROC: coprocessor error";
        case ILL_BADSTK:
          return "ILL_PRVREG: internal stack error";
        default:
          return "unknown";
      }

    case SIGFPE:
      switch (si_code) {
        case FPE_INTDIV:
          return "FPE_INTDIV: integer divide by zero";
        case FPE_INTOVF:
          return "FPE_INTOVF: integer overflow";
        case FPE_FLTDIV:
          return "FPE_FLTDIV: floating-point divide by zero";
        case FPE_FLTOVF:
          return "FPE_FLTOVF: floating-point overflow";
        case FPE_FLTUND:
          return "FPE_FLTOVF: floating-point underflow";
        case FPE_FLTRES:
          return "FPE_FLTOVF: floating-point inexact result";
        case FPE_FLTINV:
          return "FPE_FLTOVF: floating-point invalid operation";
        case FPE_FLTSUB:
          return "FPE_FLTOVF: subscript out of range";
        default:
          return "unknown";
      }

    case SIGSEGV:
      switch (si_code) {
        case SEGV_MAPERR:
          return "SEGV_MAPERR: address not mapped to object";
        case SEGV_ACCERR:
          return "SEGV_ACCERR: invalid permissions for mapped object";
        default:
          return "unknown";
      }

    case SIGBUS:
      switch (si_code) {
        case BUS_ADRALN:
          return "BUS_ADRALN: invalid address alignment";
        case BUS_ADRERR:
          return "BUS_ADRERR: nonexistent physical address";
        case BUS_OBJERR:
          return "BUS_OBJERR: object-specific hardware error";
#if 0
		case BUS_MCEERR_AR:
			return "BUS_MCEERR_AR: Hardware memory error consumed on a machine check";
		case BUS_MCEERR_AO:
			return "BUS_MCEERR_AO: Hardware memory error detected in process but not consumed";
#endif
        default:
          return "unknown";
      }
  };

  return "si_code unavailable for unknown signal";
}

/* addr2line must be available.
 * prints out the code lines [lineno - context, lineno + context] */
static std::string print_code(char *symbol, int context) {
  std::ostringstream ret;
  char executable[1024];
  char addr[1024];

  char cmd[1024];

  FILE *proc;

  /* Symbol examples:
   * ./bessd(run_worker+0x8e) [0x419d0e]" */
  /* ./bessd() [0x4149d8] */
  sscanf(symbol, "%[^(](%*s [%[^]]]", executable, addr);

  sprintf(cmd, "addr2line -i -f -p -e %s %s 2> /dev/null", executable, addr);

  proc = popen(cmd, "r");
  if (!proc) {
    return "";
  }

  for (;;) {
    char line[1024];
    char *p;

    char *filename;
    int lineno;
    int curr;

    FILE *fp;

    p = fgets(line, sizeof(line), proc);
    if (!p) {
      break;
    }

    if (line[strlen(line) - 1] != '\n') {
      // no LF found (line is too long?)
      continue;
    }

    // addr2line examples:
    // sched_free at /home/sangjin/.../tc.c:277 (discriminator 2)
    // run_worker at /home/sangjin/bess/core/module.c:653

    line[strlen(line) - 1] = '\0';

    ret << "    " << line << std::endl;

    if (line[strlen(line) - 1] == ')') {
      *(strstr(line, " (discriminator")) = '\0';
    }

    p = strrchr(p, ' ');
    if (!p) {
      continue;
    }

    p++;  // now p points to the last token (filename)

    filename = strtok(p, ":");

    if (strcmp(filename, "??") == 0) {
      continue;
    }

    p = strtok(nullptr, "");
    if (!p) {
      continue;
    }

    lineno = atoi(p);

    fp = fopen(filename, "r");
    if (!fp) {
      ret << "        (file/line not available)" << std::endl;
      continue;
    }

    for (curr = 1; !feof(fp); curr++) {
      bool discard = true;

      if (abs(curr - lineno) <= context) {
        ret << "      " << (curr == lineno ? "->" : "  ") << " " << curr
            << ": ";
        discard = false;
      } else if (curr > lineno + context) {
        break;
      }

      while (true) {
        p = fgets(line, sizeof(line), fp);
        if (!p) {
          break;
        }

        if (!discard) {
          ret << line;
        }

        if (line[strlen(line) - 1] != '\n') {
          if (feof(fp)) {
            ret << std::endl;
          }
        } else {
          break;
        }
      }
    }

    fclose(fp);
  }

  pclose(proc);

  return ret.str();
}

[[noreturn]] static void panic(const std::string &message) {
  LOG(FATAL) << message;
  exit(EXIT_FAILURE);
}

[[noreturn]] static void trap_handler(int sig_num, siginfo_t *info,
                                      void *ucontext) {
  std::ostringstream oops;
  void *addrs[STACK_DEPTH];
  void *ip;
  char **symbols;

  int cnt;
  int i;

  struct ucontext *uc;

  int skips;

  uc = (struct ucontext *)ucontext;

#if __i386
  ip = (void *)uc->uc_mcontext.gregs[REG_EIP];
#elif __x86_64
  ip = (void *)uc->uc_mcontext.gregs[REG_RIP];
#else
#  error neither x86 or x86-64
#endif

  oops << "A critical error has occured. Aborting... (pid=" << getpid()
             << ", tid=" << (pid_t)syscall(SYS_gettid) << ")" << std::endl;
  oops << "Signal: " << sig_num << " (" << strsignal(sig_num)
       << "), si_code: " << info->si_code << " ("
       << si_code_to_str(sig_num, info->si_code)
       << "), address: " << info->si_addr << ", IP: " << ip << std::endl;

  /* the linker requires -rdynamic for non-exported symbols */
  cnt = backtrace(addrs, STACK_DEPTH);
  if (cnt < 3) {
    oops << "ERROR: backtrace() failed" << std::endl;
    panic(oops.str());
  }

  /* addrs[0]: this function
   * addrs[1]: sigaction in glibc
   * addrs[2]: the trigerring instruction pointer *or* its caller
   *           (depending on the kernel behavior?) */
  if (addrs[2] == ip) {
    skips = 2;
  } else {
    addrs[1] = ip;
    skips = 1;
  }

  /* The return addresses point to the next instruction
   * after its call, so adjust. */
  for (i = skips + 1; i < cnt; i++)
    addrs[i] = (void *)((uintptr_t)addrs[i] - 1);

  symbols = backtrace_symbols(addrs, cnt);

  if (symbols) {
    oops << "Backtrace (recent calls first) ---" << std::endl;

    for (i = skips; i < cnt; ++i) {
      oops << "(" << i - skips << "): " << symbols[i] << std::endl;
      oops << print_code(symbols[i], (i == skips) ? 3 : 0);
    }

    free(symbols); /* required by backtrace_symbols() */
  } else
    oops << "ERROR: backtrace_symbols() failed\n";

  panic(oops.str());
}

__attribute__((constructor(101))) static void set_trap_handler() {
  const int signals[] = {
      SIGSEGV, SIGBUS,  SIGILL,
      SIGFPE,  SIGABRT, SIGUSR1, /* for users to trigger */
  };

  const int ignored_signals[] = {
      SIGPIPE,
  };
  struct sigaction sigact;
  size_t i;

  sigact.sa_sigaction = trap_handler;
  sigact.sa_flags = SA_RESTART | SA_SIGINFO;

  for (i = 0; i < sizeof(signals) / sizeof(int); i++) {
    int ret = sigaction(signals[i], &sigact, nullptr);
    assert(ret != 1);
  }

  for (i = 0; i < sizeof(ignored_signals) / sizeof(int); i++) {
    signal(ignored_signals[i], SIG_IGN);
  }
}

void dump_types(void) {
  printf("gcc: %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
  printf("glibc: %s-%s\n", gnu_get_libc_version(), gnu_get_libc_release());
  printf("DPDK: %s\n", rte_version());

  printf("sizeof(char)=%zu\n", sizeof(char));
  printf("sizeof(short)=%zu\n", sizeof(short));
  printf("sizeof(int)=%zu\n", sizeof(int));
  printf("sizeof(long)=%zu\n", sizeof(long));
  printf("sizeof(long long)=%zu\n", sizeof(long long));
  printf("sizeof(intmax_t)=%zu\n", sizeof(intmax_t));
  printf("sizeof(void *)=%zu\n", sizeof(void *));
  printf("sizeof(size_t)=%zu\n", sizeof(size_t));

  printf("sizeof(heap)=%zu\n", sizeof(struct heap));
  printf("sizeof(HTableBase)=%zu\n", sizeof(HTableBase));
  printf("sizeof(clist_head)=%zu sizeof(cdlist_item)=%zu\n",
         sizeof(struct cdlist_head), sizeof(struct cdlist_item));

  printf("sizeof(rte_mbuf)=%zu\n", sizeof(struct rte_mbuf));
  printf("sizeof(snbuf)=%zu\n", sizeof(struct snbuf));
  printf("sizeof(pkt_batch)=%zu\n", sizeof(struct pkt_batch));
  printf("sizeof(sched)=%zu sizeof(sched_stats)=%zu\n", sizeof(struct sched),
         sizeof(struct sched_stats));
  printf("sizeof(tc)=%zu sizeof(tc_stats)=%zu\n", sizeof(struct tc),
         sizeof(struct tc_stats));
  printf("sizeof(task)=%zu\n", sizeof(struct task));

  printf("sizeof(Module)=%zu\n", sizeof(Module));
  printf("sizeof(gate)=%zu\n", sizeof(struct gate));

  printf("sizeof(worker_context)=%zu\n", sizeof(Worker));

  printf("sizeof(snobj)=%zu\n", sizeof(struct snobj));
}
