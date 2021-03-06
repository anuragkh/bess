#include "ether_encap.h"

enum {
  ATTR_R_ETHER_SRC,
  ATTR_R_ETHER_DST,
  ATTR_R_ETHER_TYPE,
};

const Commands<Module> EtherEncap::cmds = {};
const PbCommands<Module> EtherEncap::pb_cmds = {};

void EtherEncap::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    struct snbuf *pkt = batch->pkts[i];

    struct ether_addr ether_src;
    struct ether_addr ether_dst;
    uint16_t ether_type;

    ether_src = get_attr<struct ether_addr>(this, ATTR_R_ETHER_SRC, pkt);
    ether_dst = get_attr<struct ether_addr>(this, ATTR_R_ETHER_DST, pkt);
    ether_type = get_attr<uint16_t>(this, ATTR_R_ETHER_TYPE, pkt);

    struct ether_hdr *ethh =
        static_cast<struct ether_hdr *>(snb_prepend(pkt, sizeof(*ethh)));

    if (unlikely(!ethh)) {
      continue;
    }

    ethh->d_addr = ether_dst;
    ethh->s_addr = ether_src;
    ethh->ether_type = ether_type;
  }

  RunNextModule(batch);
}

ADD_MODULE(EtherEncap, "ether_encap",
           "encapsulates packets with an Ethernet header")
