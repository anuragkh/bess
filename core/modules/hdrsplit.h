#include "../module.h"

class HeaderSplit : public Module {
 public:
  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 2;
  
  static const gate_idx_t kRegOut = 0;
  static const gate_idx_t kHdrOut = 1;
  
  static const size_t kMaxHdrSize = 138;

  static const Commands<Module> cmds;
  static const PbCommands pb_cmds;
};