#include <arpa/inet.h>

#include <rte_byteorder.h>
#include <rte_config.h>
#include <rte_errno.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>

#include "hdrsplit.h"

const Commands<Module> HeaderSplit::cmds = { };
const PbCommands<Module> HeaderSplit::pb_cmds = { };

void HeaderSplit::ProcessBatch(struct pkt_batch *batch) {
  int cnt = batch->cnt;
  
  struct pkt_batch hdr_batch;
  hdr_batch.cnt = snb_alloc_bulk(hdr_batch.pkts, cnt, kMaxHdrSize);

  for (int i = 0; i < cnt; i++) {
    struct snbuf *hdr = hdr_batch.pkts[i];
    
    struct snbuf *pkt = batch->pkts[i];
    void *pkt_data = snb_head_data(pkt);
    struct ether_hdr *ethh = static_cast<struct ether_hdr *>(pkt_data);
    struct ipv4_hdr *iph = reinterpret_cast<struct ipv4_hdr *>(ethh + 1);
    int iph_bytes = (iph->version_ihl & 0xf) << 2;
    
    if (iph->next_proto_id == IPPROTO_TCP) {
      struct tcp_hdr *tcph = reinterpret_cast<struct tcp_hdr *>(iph + 1);
      uint16_t size = sizeof(*ethh) + iph_bytes + sizeof(*tcph);
      char* ptr = static_cast<char *>(hdr->mbuf.buf_addr) + SNBUF_HEADROOM;
      
      hdr->mbuf.data_off = SNBUF_HEADROOM;
      hdr->mbuf.pkt_len = size;
      hdr->mbuf.data_len = size;
      
      memcpy_sloppy(ptr, pkt_data, size);
    } else if (iph->next_proto_id == IPPROTO_UDP) {
      struct udp_hdr *udph = reinterpret_cast<struct udp_hdr *>(iph + 1);
      uint16_t size = sizeof(*ethh) + iph_bytes + sizeof(*udph);
      char* ptr = static_cast<char *>(hdr->mbuf.buf_addr) + SNBUF_HEADROOM;
      
      hdr->mbuf.data_off = SNBUF_HEADROOM;
      hdr->mbuf.pkt_len = size;
      hdr->mbuf.data_len = size;
      
      memcpy_sloppy(ptr, pkt_data, size);
    }
  }
  
  RunChooseModule(kRegOut, batch);
  RunChooseModule(kHdrOut, &hdr_batch);
}

ADD_MODULE(HeaderSplit, "hdr_split",
           "split packets into two streams: unchanged, and header data only")