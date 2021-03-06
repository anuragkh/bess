#ifndef BESS_MODULES_UPDATE_H_
#define BESS_MODULES_UPDATE_H_

#include "../module.h"

#define UPDATE_MAX_FIELDS 16

class Update : public Module {
 public:
  Update() : Module(), num_fields_(), fields_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  struct snobj *CommandAdd(struct snobj *arg);
  struct snobj *CommandClear(struct snobj *arg);

  bess::pb::ModuleCommandResponse CommandAdd(const google::protobuf::Any &arg);
  bess::pb::ModuleCommandResponse CommandClear(
      const google::protobuf::Any &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  int num_fields_ = {};

  struct {
    uint64_t mask;  /* bits with 1 won't be updated */
    uint64_t value; /* in network order */
    int16_t offset;
  } fields_[UPDATE_MAX_FIELDS];
};

#endif  // BESS_MODULES_UPDATE_H_
