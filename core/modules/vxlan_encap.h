#ifndef BESS_MODULES_VXLANENCAP_H_
#define BESS_MODULES_VXLANENCAP_H_

#include "../module.h"

class VXLANEncap : public Module {
 public:
  VXLANEncap() : Module(), dstport_() {}

  virtual struct snobj *Init(struct snobj *arg);
  virtual pb_error_t Init(const google::protobuf::Any &arg);
  virtual void ProcessBatch(struct pkt_batch *batch);

  size_t num_attrs = 6;
  struct bess::metadata::mt_attr attrs[bess::metadata::kMaxAttrsPerModule] = {
      {
          .name = "tun_ip_src",
          .size = 4,
          .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "tun_ip_dst",
          .size = 4,
          .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "tun_id", .size = 4, .mode = bess::metadata::AccessMode::READ,
      },
      {
          .name = "ip_src",
          .size = 4,
          .mode = bess::metadata::AccessMode::WRITE,
      },
      {
          .name = "ip_dst",
          .size = 4,
          .mode = bess::metadata::AccessMode::WRITE,
      },
      {
          .name = "ip_proto",
          .size = 1,
          .mode = bess::metadata::AccessMode::WRITE,
      },
  };

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

 private:
  uint16_t dstport_;
};

#endif  // BESS_MODULES_VXLANENCAP_H_
