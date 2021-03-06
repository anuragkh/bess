#include "noop.h"

const Commands<Module> NoOP::cmds = {};
const PbCommands<Module> NoOP::pb_cmds = {};

struct snobj *NoOP::Init(struct snobj *) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return snobj_err(ENOMEM, "Task creation failed");

  return nullptr;
}

pb_error_t NoOP::Init(const google::protobuf::Any &) {
  task_id_t tid;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID)
    return pb_error(ENOMEM, "Task creation failed");

  return pb_errno(0);
}

struct task_result NoOP::RunTask(void *) {
  return {};
}

ADD_MODULE(NoOP, "noop", "creates a task that does nothing")
