#include "metadata.h"

#include <algorithm>
#include <functional>
#include <queue>

#include <glog/logging.h>

#include "module.h"

namespace bess {
namespace metadata {

// TODO: Once the rest of the code supports multiple pipelines, this ought to be
// a collection of pipelines in bess::metadata a la Ports/Modules.
Pipeline default_pipeline;

// Helpers -----------------------------------------------------------------

static mt_offset_t ComputeNextOffset(mt_offset_t curr_offset, int8_t size) {
  uint32_t overflow;
  int8_t rounded_size;

  rounded_size = align_ceil_pow2(size);

  if (curr_offset % rounded_size) {
    curr_offset = align_ceil(curr_offset, rounded_size);
  }

  overflow = (uint32_t)curr_offset + (uint32_t)size;

  return overflow > kMetadataTotalSize ? kMetadataOffsetNoSpace : curr_offset;
}

// Generate warnings for modules that read metadata that never gets set.
static void CheckOrphanReaders() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    const Module *m = it.second;
    if (!m) {
      break;
    }

    for (size_t i = 0; i < m->num_attrs; i++) {
      if (m->attr_offsets[i] != kMetadataOffsetNoRead) {
        continue;
      }

      LOG(WARNING) << "Metadata attr " << m->attrs[i].name << "/"
                   << m->attrs[i].size << " of module " << m->name() << " has "
                   << "no upstream module that sets the value!";
    }
  }
}

static inline attr_id_t get_attr_id(const struct mt_attr *attr) {
  return attr->name;
}

// ScopeComponent ----------------------------------------------------------

class ScopeComponentComp {
 public:
  ScopeComponentComp(const bool &revparam = false) { reverse_ = revparam; }

  bool operator()(const ScopeComponent *lhs, const ScopeComponent *rhs) const {
    if (reverse_) {
      return (lhs->offset() < rhs->offset());
    }
    return (lhs->offset() > rhs->offset());
  }

 private:
  bool reverse_;
};

static int DegreeComp(const ScopeComponent &a, const ScopeComponent &b) {
  return b.degree() - a.degree();
}

bool ScopeComponent::DisjointFrom(const ScopeComponent &rhs) {
  for (const auto &i : modules_) {
    for (const auto &j : rhs.modules_) {
      if (i == j) {
        return 0;
      }
    }
  }
  return 1;
}

// Pipeline ----------------------------------------------------------------

int Pipeline::PrepareMetadataComputation() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    Module *m = it.second;
    if (!m) {
      break;
    }

    if (!module_components_.count(m)) {
      module_components_.emplace(
          m, reinterpret_cast<scope_id_t *>(
                 mem_alloc(sizeof(scope_id_t) * kMetadataTotalSize)));
    }

    if (module_components_[m] == nullptr) {
      return -ENOMEM;
    }

    module_scopes_[m] = -1;
    memset(module_components_[m], -1, sizeof(scope_id_t) * kMetadataTotalSize);

    for (size_t i = 0; i < m->num_attrs; i++) {
      m->attrs[i].scope_id = -1;
    }
  }
  return 0;
}

void Pipeline::CleanupMetadataComputation() {
  for (const auto &it : module_components_) {
    mem_free(module_components_[it.first]);
  }
  module_components_.clear();
  module_scopes_.clear();

  attributes_.clear();

  for (auto &c : scope_components_) {
    c.clear_modules();
  }
  scope_components_.clear();
}

void Pipeline::AddModuleToComponent(Module *m, struct mt_attr *attr) {
  ScopeComponent &component = scope_components_.back();

  // Module has already been added to current scope component.
  if (std::find(component.modules().begin(), component.modules().end(), m) !=
      component.modules().end()) {
    return;
  }

  if (component.modules().empty()) {
    component.set_attr_id(get_attr_id(attr));
    component.set_size(attr->size);
  }
  component.add_module(m);
}

struct mt_attr *Pipeline::FindAttr(Module *m, struct mt_attr *attr) {
  struct mt_attr *curr_attr;

  for (size_t i = 0; i < m->num_attrs; i++) {
    curr_attr = &m->attrs[i];
    if (get_attr_id(curr_attr) == get_attr_id(attr)) {
      return curr_attr;
    }
  }

  return nullptr;
}

void Pipeline::TraverseUpstream(Module *m, struct mt_attr *attr) {
  struct mt_attr *found_attr;

  AddModuleToComponent(m, attr);
  found_attr = FindAttr(m, attr);

  /* end of scope component */
  if (found_attr && found_attr->mode == AccessMode::WRITE) {
    if (found_attr->scope_id == -1)
      IdentifyScopeComponent(m, found_attr);
    return;
  }

  /* cycle detection */
  if (module_scopes_[m] == static_cast<int>(scope_components_.size())) {
    return;
  }
  module_scopes_[m] = static_cast<int>(scope_components_.size());

  for (int i = 0; i < m->igates.curr_size; i++) {
    struct gate *g = m->igates.arr[i];
    struct gate *og;

    cdlist_for_each_entry(og, &g->in.ogates_upstream, out.igate_upstream) {
      TraverseUpstream(og->m, attr);
    }
  }

  if (m->igates.curr_size == 0) {
    scope_components_.back().set_invalid(true);
  }
}

int Pipeline::TraverseDownstream(Module *m, struct mt_attr *attr) {
  struct gate *ogate;
  struct mt_attr *found_attr;
  int8_t in_scope = 0;

  // cycle detection
  if (module_scopes_[m] == static_cast<int>(scope_components_.size())) {
    return -1;
  }
  module_scopes_[m] = static_cast<int>(scope_components_.size());

  found_attr = FindAttr(m, attr);

  if (found_attr && (found_attr->mode == AccessMode::READ ||
                     found_attr->mode == AccessMode::UPDATE)) {
    AddModuleToComponent(m, found_attr);
    found_attr->scope_id = scope_components_.size();

    for (int i = 0; i < m->ogates.curr_size; i++) {
      ogate = m->ogates.arr[i];
      if (!ogate) {
        continue;
      }

      TraverseDownstream(ogate->out.igate->m, attr);
    }

    module_scopes_[m] = -1;
    TraverseUpstream(m, attr);
    in_scope = 1;
    return 0;
  } else if (found_attr) {
    module_scopes_[m] = -1;
    in_scope = 0;
    return -1;
  }

  for (int i = 0; i < m->ogates.curr_size; i++) {
    ogate = m->ogates.arr[i];
    if (!ogate) {
      continue;
    }

    if (TraverseDownstream(ogate->out.igate->m, attr) != -1) {
      in_scope = 1;
    }
  }

  if (in_scope) {
    AddModuleToComponent(m, attr);
    module_scopes_[m] = -1;
    TraverseUpstream(m, attr);
  }

  return in_scope ? 0 : -1;
}

void Pipeline::IdentifySingleScopeComponent(Module *m, struct mt_attr *attr) {
  scope_components_.emplace_back();
  IdentifyScopeComponent(m, attr);
  scope_components_.back().set_scope_id(scope_components_.size());
}

void Pipeline::IdentifyScopeComponent(Module *m, struct mt_attr *attr) {
  struct gate *ogate;

  AddModuleToComponent(m, attr);
  attr->scope_id = scope_components_.size();

  /* cycle detection */
  module_scopes_[m] = static_cast<int>(scope_components_.size());

  for (int i = 0; i < m->ogates.curr_size; i++) {
    ogate = m->ogates.arr[i];
    if (!ogate) {
      continue;
    }

    TraverseDownstream(ogate->out.igate->m, attr);
  }
}

void Pipeline::FillOffsetArrays() {
  for (size_t i = 0; i < scope_components_.size(); i++) {
    const std::set<Module *> &modules = scope_components_[i].modules();
    const attr_id_t &id = scope_components_[i].attr_id();
    int size = scope_components_[i].size();
    mt_offset_t offset = scope_components_[i].offset();
    uint8_t invalid = scope_components_[i].invalid();

    // attr not read donwstream.
    if (modules.size() == 1) {
      scope_components_[i].set_offset(kMetadataOffsetNoWrite);
      offset = kMetadataOffsetNoWrite;
    }

    for (Module *m : modules) {
      for (size_t k = 0; k < m->num_attrs; k++) {
        if (get_attr_id(&m->attrs[k]) == id) {
          if (invalid && m->attrs[k].mode == AccessMode::READ) {
            m->attr_offsets[k] = kMetadataOffsetNoRead;
          } else if (invalid) {
            m->attr_offsets[k] = kMetadataOffsetNoWrite;
          } else {
            m->attr_offsets[k] = offset;
          }
          break;
        }
      }

      if (!invalid && offset >= 0) {
        for (int l = 0; l < size; l++) {
          module_components_[m][offset + l] = i;
        }
      }
    }
  }
}

void Pipeline::AssignOffsets() {
  mt_offset_t offset = 0;
  ScopeComponent *comp1;
  const ScopeComponent *comp2;

  for (size_t i = 0; i < scope_components_.size(); i++) {
    std::priority_queue<const ScopeComponent *, std::vector<const ScopeComponent *>,
                        ScopeComponentComp>
        h;
    comp1 = &scope_components_[i];

    if (comp1->invalid()) {
      comp1->set_offset(kMetadataOffsetNoRead);
      comp1->set_assigned(true);
      continue;
    }

    if (comp1->assigned() || comp1->modules().size() == 1) {
      continue;
    }

    offset = 0;

    for (size_t j = 0; j < scope_components_.size(); j++) {
      if (i == j) {
        continue;
      }

      if (!scope_components_[i].DisjointFrom(scope_components_[j]) &&
          scope_components_[j].assigned()) {
        h.push(&scope_components_[j]);
      }
    }

    while (!h.empty()) {
      comp2 = h.top();
      h.pop();

      if (comp2->offset() == kMetadataOffsetNoRead ||
          comp2->offset() == kMetadataOffsetNoWrite ||
          comp2->offset() == kMetadataOffsetNoSpace) {
        continue;
      }

      if (offset + comp1->size() > comp2->offset()) {
        offset =
            ComputeNextOffset(comp2->offset() + comp2->size(), comp1->size());
      } else {
        break;
      }
    }

    comp1->set_offset(offset);
    comp1->set_assigned(true);
  }

  FillOffsetArrays();
}

void Pipeline::LogAllScopesPerModule() {
  for (const auto &it : ModuleBuilder::all_modules()) {
    const Module *m = it.second;
    if (!m) {
      break;
    }

    LOG(INFO) << "Module " << m->name()
              << " part of the following scope components: ";
    for (size_t i = 0; i < kMetadataTotalSize; i++) {
      if (module_components_[m][i] != -1) {
        LOG(INFO) << "scope " << module_components_[m][i] << " at offset " << i;
      }
    }
  }
}

void Pipeline::ComputeScopeDegrees() {
  for (size_t i = 0; i < scope_components_.size(); i++) {
    for (size_t j = i + 1; j < scope_components_.size(); j++) {
      if (!scope_components_[i].DisjointFrom(scope_components_[j])) {
        scope_components_[i].incr_degree();
        scope_components_[j].incr_degree();
      }
    }
  }
}

/* Main entry point for calculating metadata offsets. */
int Pipeline::ComputeMetadataOffsets() {
  int ret;

  ret = PrepareMetadataComputation();

  if (ret) {
    CleanupMetadataComputation();
    return ret;
  }

  for (const auto &it : ModuleBuilder::all_modules()) {
    Module *m = it.second;
    if (!m) {
      break;
    }

    for (size_t i = 0; i < m->num_attrs; i++) {
      struct mt_attr *attr = &m->attrs[i];

      if (attr->mode == AccessMode::READ || attr->mode == AccessMode::UPDATE) {
        m->attr_offsets[i] = kMetadataOffsetNoRead;
      } else if (attr->mode == AccessMode::WRITE) {
        m->attr_offsets[i] = kMetadataOffsetNoWrite;
      }

      if (attr->mode == AccessMode::WRITE && attr->scope_id == -1) {
        IdentifySingleScopeComponent(m, attr);
      }
    }
  }

  ComputeScopeDegrees();
  std::sort(scope_components_.begin(), scope_components_.end(), DegreeComp);
  AssignOffsets();

  for (size_t i = 0; i < scope_components_.size(); i++) {
    LOG(INFO) << "scope component for " << scope_components_[i].size()
              << "-byte"
              << "attr " << scope_components_[i].attr_id() << "at offset "
              << scope_components_[i].offset() << ": {";

    for (const auto &it : scope_components_[i].modules()) {
      LOG(INFO) << it->name();
    }

    LOG(INFO) << "}";
  }

  LogAllScopesPerModule();

  CheckOrphanReaders();

  CleanupMetadataComputation();
  return 0;
}

int Pipeline::RegisterAttribute(const struct mt_attr *attr) {
  attr_id_t id = get_attr_id(attr);
  const auto &it = attributes_.find(id);
  if (it == attributes_.end()) {
    attributes_.emplace(id, attr);
    return 0;
  }
  return (it->second->size == attr->size) ? 0 : -EEXIST;
}

}  // namespace metadata
}  // namespace bess
