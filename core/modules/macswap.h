#ifndef BESS_MODULES_MACSWAP_H_
#define BESS_MODULES_MACSWAP_H_

#include "../module.h"

class MACSwap : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;
};

#endif  // BESS_MODULES_MACSWAP_H_
