#ifndef BESS_MODULES_MTTEST_H_
#define BESS_MODULES_MTTEST_H_

#include "../module.h"

class MetadataTest : public Module {
 public:
  struct snobj *Init(struct snobj *arg);
  pb_error_t Init(const google::protobuf::Any &arg);

  void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = MAX_GATES;
  static const gate_idx_t kNumOGates = MAX_GATES;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  struct snobj *AddAttributes(struct snobj *attrs,
                              bess::metadata::AccessMode mode);
  pb_error_t AddAttributes(
      const google::protobuf::Map<std::string, int64_t> &attrs,
      bess::metadata::AccessMode mode);
};

#endif  // BESS_MODULES_MTTEST_H_
