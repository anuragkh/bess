#include "source.h"

const Commands<Module> Source::cmds = {
    {"set_pkt_size", MODULE_FUNC &Source::command_set_pkt_size, 1},
    {"set_burst", MODULE_FUNC &Source::command_set_burst, 1},
};

const PbCommands<Module> Source::pb_cmds = {
    {"set_pkt_size", PB_MODULE_FUNC &Source::command_set_pkt_size, 1},
    {"set_burst", PB_MODULE_FUNC &Source::command_set_burst, 1},
};

pb_error_t Source::Init(const google::protobuf::Any &arg_) {
  bess::pb::SourceArg arg;
  arg_.UnpackTo(&arg);

  pb_error_t err;
  bess::pb::ModuleCommandResponse response;

  task_id_t tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = MAX_PKT_BURST;

  google::protobuf::Any pkt_size_arg;
  pkt_size_arg.PackFrom(arg.pkt_size_arg());
  response = CommandSetPktSize(pkt_size_arg);
  err = response.error();
  if (err.err() != 0) {
    return err;
  }

  google::protobuf::Any burst_arg;
  burst_arg.PackFrom(arg.burst_arg());
  response = CommandSetBurst(burst_arg);
  err = response.error();
  if (err.err() != 0) {
    return err;
  }

  return pb_errno(0);
}

bess::pb::ModuleCommandResponse Source::CommandSetBurst(
    const google::protobuf::Any &arg_) {
  bess::pb::SourceCommandSetBurstArg arg;
  arg_.UnpackTo(&arg);

  bess::pb::ModuleCommandResponse response;

  uint64_t val = arg.burst();
  if (val == 0 || val > MAX_PKT_BURST) {
    set_cmd_response_error(
        &response,
        pb_error(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST));
    return response;
  }
  burst_ = val;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

bess::pb::ModuleCommandResponse Source::CommandSetPktSize(
    const google::protobuf::Any &arg_) {
  bess::pb::SourceCommandSetPktSizeArg arg;
  arg_.UnpackTo(&arg);

  bess::pb::ModuleCommandResponse response;

  uint64_t val = arg.pkt_size();
  if (val == 0 || val > SNBUF_DATA) {
    set_cmd_response_error(&response, pb_error(EINVAL, "Invalid packet size"));
    return response;
  }
  pkt_size_ = val;
  set_cmd_response_error(&response, pb_errno(0));
  return response;
}

struct snobj *Source::Init(struct snobj *arg) {
  struct snobj *t;
  struct snobj *err;

  task_id_t tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "Task creation failed");

  pkt_size_ = 60;
  burst_ = MAX_PKT_BURST;

  if (!arg) {
    return nullptr;
  }

  if ((t = snobj_eval(arg, "pkt_size")) != nullptr) {
    err = command_set_pkt_size(t);
    if (err) {
      return err;
    }
  }

  if ((t = snobj_eval(arg, "burst")) != nullptr) {
    err = command_set_burst(t);
    if (err) {
      return err;
    }
  }

  return nullptr;
}

struct task_result Source::RunTask(void *) {
  struct pkt_batch batch;
  struct task_result ret;

  const int pkt_overhead = 24;

  const int pkt_size = ACCESS_ONCE(pkt_size_);
  const int burst = ACCESS_ONCE(burst_);

  uint64_t total_bytes = pkt_size * burst;

  int cnt = snb_alloc_bulk(batch.pkts, burst, pkt_size);

  if (cnt > 0) {
    batch.cnt = cnt;
    RunNextModule(&batch);
  }

  ret = (struct task_result){
      .packets = static_cast<uint64_t>(cnt),
      .bits = (total_bytes + cnt * pkt_overhead) * 8,
  };

  return ret;
}

struct snobj *Source::command_set_pkt_size(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT) {
    return snobj_err(EINVAL, "pkt_size must be an integer");
  }

  val = snobj_uint_get(arg);

  if (val == 0 || val > SNBUF_DATA) {
    return snobj_err(EINVAL, "Invalid packet size");
  }

  pkt_size_ = val;

  return nullptr;
}

struct snobj *Source::command_set_burst(struct snobj *arg) {
  uint64_t val;

  if (snobj_type(arg) != TYPE_INT) {
    return snobj_err(EINVAL, "burst must be an integer");
  }

  val = snobj_uint_get(arg);

  if (val == 0 || val > MAX_PKT_BURST) {
    return snobj_err(EINVAL, "burst size must be [1,%d]", MAX_PKT_BURST);
  }

  burst_ = val;

  return nullptr;
}

ADD_MODULE(Source, "source",
           "infinitely generates packets with uninitialized data")
