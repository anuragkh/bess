#include <arpa/inet.h>

#include <rte_byteorder.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>

#include "../module.h"
#include "../packetstore/packetstore.h"

class NetPlay : public Module {
 public:
  NetPlay();

  virtual void ProcessBatch(struct pkt_batch *batch);

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;

 private:
  netplay::packet_store::handle* handle_;
  netplay::packet_store store_;
};

const Commands<Module> NetPlay::cmds = { };

NetPlay::NetPlay() {
  handle_ = store_.get_handle();
}

void NetPlay::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;

  for (int i = 0; i < cnt; i++) {
    slog::token_list tokens;
    uint32_t num_bytes = 0;

    void* pkt = snb_head_data(batch->pkts[i]);
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    num_bytes += sizeof(struct ether_hdr);

    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    num_bytes += sizeof(struct ipv4_hdr);

    handle_->add_src_ip(tokens, ip->src_addr);
    handle_->add_dst_ip(tokens, ip->dst_addr);
    if (ip->next_proto_id == IPPROTO_TCP) {
      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      num_bytes += sizeof(struct tcp_hdr);

      handle_->add_src_port(tokens, tcp->src_port);
      handle_->add_dst_port(tokens, tcp->dst_port);
    } else if (ip->next_proto_id == IPPROTO_UDP) {
      struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
      num_bytes += sizeof(struct udp_hdr);

      handle_->add_src_port(tokens, udp->src_port);
      handle_->add_dst_port(tokens, udp->dst_port);
    }
    handle_->insert((unsigned char*) pkt, num_bytes, tokens);
  }
  RunNextModule(batch);
}

ADD_MODULE(NetPlay, "netplay", "Indexes packet header data")

