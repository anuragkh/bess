#include "../module.h"

class NetPlay : public Module {
 public:
  NetPlay();

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  netplay::packet_store::handle* handle_;
  netplay::packet_store store_;
};