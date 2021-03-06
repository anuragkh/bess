#ifndef BESS_MODULES_VLANPUSH_H_
#define BESS_MODULES_VLANPUSH_H_

#include "../module.h"

class VLANPush : public Module {
 public:
  VLANPush() : Module(), vlan_tag_(), qinq_tag_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);

  virtual void ProcessBatch(struct pkt_batch *batch);

  virtual std::string GetDesc() const;

  struct snobj *CommandSetTci(struct snobj *arg);
  bess::pb::ModuleCommandResponse CommandSetTci(
      const google::protobuf::Any &arg);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  /* network order */
  uint32_t vlan_tag_;
  uint32_t qinq_tag_;
};

#endif  // BESS_MODULES_VLANPUSH_H_
