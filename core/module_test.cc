#include "module.h"

#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#include "utils/cdlist.h"

namespace {

// Mocking out misc things  ------------------------------------------------

class AcmeModule : public Module {
 public:
  AcmeModule() : Module() {}

  static const gate_idx_t kNumIGates = 1;
  static const gate_idx_t kNumOGates = 1;

  static const Commands<Module> cmds;
  static const PbCommands<Module> pb_cmds;

  struct snobj *Foo(struct snobj *) {
    n += 1;
    return nullptr;
  }

  bess::pb::ModuleCommandResponse Foo(const google::protobuf::Any &) {
    n += 1;
    return bess::pb::ModuleCommandResponse();
  }

  int n = {};
};

const Commands<Module> AcmeModule::cmds = {
    {"foo", MODULE_FUNC &AcmeModule::Foo, 0}};

const PbCommands<Module> AcmeModule::pb_cmds = {
    {"foo", PB_MODULE_FUNC &AcmeModule::Foo, 0}};

// Simple harness for testing the Module class.
class ModuleTester : public ::testing::Test {
 protected:
  virtual void SetUp() {
    ADD_MODULE(AcmeModule, "acme_module", "foo bar");
    ASSERT_TRUE(__module__AcmeModule);
  }

  virtual void TearDown() {
    ModuleBuilder::DestroyAllModules();
    ModuleBuilder::all_module_builders_holder(true);
  }
};

int create_acme(const char *name, Module **m) {
  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  std::string mod_name;
  if (name) {
    if (ModuleBuilder::all_modules().count(name)) {
      return EEXIST;
    }
    mod_name = name;
  } else {
    mod_name = ModuleBuilder::GenerateDefaultName(builder.class_name(),
                                                  builder.name_template());
  }

  *m = builder.CreateModule(mod_name, &bess::metadata::default_pipeline);
  builder.AddModule(*m);

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.cmds().size());

  return 0;
}

// Check that new module classes are actually created correctly and stored in
// the table of module classes
TEST(ModuleBuilderTest, RegisterModuleClass) {
  size_t num_builders = ModuleBuilder::all_module_builders().size();
  ADD_MODULE(AcmeModule, "acme_module", "foo bar");
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(num_builders + 1, ModuleBuilder::all_module_builders().size());
  ASSERT_EQ(1, ModuleBuilder::all_module_builders().count("AcmeModule"));

  const ModuleBuilder &builder =
      ModuleBuilder::all_module_builders().find("AcmeModule")->second;

  EXPECT_EQ("AcmeModule", builder.class_name());
  EXPECT_EQ("acme_module", builder.name_template());
  EXPECT_EQ("foo bar", builder.help_text());
  EXPECT_EQ(1, builder.NumIGates());
  EXPECT_EQ(1, builder.NumOGates());
  EXPECT_EQ(1, builder.cmds().size());

  ModuleBuilder::all_module_builders_holder(true);
}

TEST(ModuleBuilderTest, GenerateDefaultNameTemplate) {
  std::string name1 = ModuleBuilder::GenerateDefaultName("FooBar", "foo");
  EXPECT_EQ("foo0", name1);

  std::string name2 = ModuleBuilder::GenerateDefaultName("FooBar", "");
  EXPECT_EQ("foo_bar0", name2);

  std::string name3 = ModuleBuilder::GenerateDefaultName("FooABCBar", "");
  EXPECT_EQ("foo_abcbar0", name3);
}

// Check that module builders create modules correctly when given a name
TEST_F(ModuleTester, CreateModuleWithName) {
  Module *m1, *m2;
  ADD_MODULE(AcmeModule, "acme_module", "foo bar");
  ASSERT_TRUE(__module__AcmeModule);

  EXPECT_EQ(0, create_acme("bar", &m1));
  ASSERT_NE(nullptr, m1);
  EXPECT_EQ(1, ModuleBuilder::all_modules().size());
  EXPECT_EQ(EEXIST, create_acme("bar", &m2));
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("bar"));
}

// Check that module builders create modules with generated names
TEST_F(ModuleTester, CreateModuleGenerateName) {
  Module *m;

  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  EXPECT_EQ(1, ModuleBuilder::all_modules().size());
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("acme_module0"));
  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  EXPECT_EQ(2, ModuleBuilder::all_modules().size());
  EXPECT_EQ(1, ModuleBuilder::all_modules().count("acme_module1"));
}

TEST_F(ModuleTester, RunCommand) {
  Module *m;

  EXPECT_EQ(0, create_acme(nullptr, &m));
  ASSERT_NE(nullptr, m);
  for (int i = 0; i < 10; i++) {
    m->RunCommand("foo", nullptr);
  }
  EXPECT_EQ(10, ((AcmeModule *)m)->n);
}

TEST_F(ModuleTester, ConnectModules) {
  Module *m1, *m2;
  struct gate *og;

  EXPECT_EQ(0, create_acme("m1", &m1));
  ASSERT_NE(nullptr, m1);
  EXPECT_EQ(0, create_acme("m2", &m2));
  ASSERT_NE(nullptr, m2);

  EXPECT_EQ(0, m1->ConnectModules(0, m2, 0));
  EXPECT_EQ(1, m1->ogates.curr_size);
  EXPECT_EQ(m2, m1->ogates.arr[0]->out.igate->m);
  EXPECT_EQ(1, m2->igates.curr_size);

  cdlist_for_each_entry(og, &m2->igates.arr[0]->in.ogates_upstream,
                        out.igate_upstream) {
    ASSERT_NE(nullptr, og);
    EXPECT_EQ(m1, og->m);
  }
}

TEST_F(ModuleTester, ResetModules) {
  Module *m;

  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(0, create_acme(nullptr, &m));
    ASSERT_NE(nullptr, m);
  }
  EXPECT_EQ(10, ModuleBuilder::all_modules().size());

  ModuleBuilder::DestroyAllModules();
  EXPECT_EQ(0, ModuleBuilder::all_modules().size());
}

}  // namespace (unnamed)
