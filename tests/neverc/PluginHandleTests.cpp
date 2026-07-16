#include "neverc/Plugin/Host/PluginHandleArena.h"
#include "neverc/Plugin/Host/PluginProcessServices.h"
#include "neverc/Plugin/Host/PluginRegistration.h"
#include "neverc/Plugin/Host/PluginSession.h"
#include "neverc/Plugin/Host/PluginTaskContext.h"
#include "gtest/gtest.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Error.h"
#include <array>
#include <memory>
#include <string>

using namespace llvm;
using namespace neverc::plugin;

namespace {

std::string takeErrorMessage(Error ErrorValue) {
  auto Message = toString(std::move(ErrorValue));
  return Message.str().str();
}

std::unique_ptr<PluginSession>
createSession(PluginProcessServices &Services) {
  const std::array<StringRef, 1> Selected = {
      "org.neverc.test.scope.session"};
  auto Plan = makePluginActivationPlan(Services.registry(), Selected);
  if (!Plan) {
    ADD_FAILURE() << takeErrorMessage(Plan.takeError());
    return nullptr;
  }
  auto Session = PluginSession::create(Services, *Plan);
  if (!Session) {
    ADD_FAILURE() << takeErrorMessage(Session.takeError());
    return nullptr;
  }
  return std::move(*Session);
}

TEST(PluginHandleArenaTest, RejectsWrongOwnerScopeTypeAndStaleGeneration) {
  PluginProcessServices Services("neverc-plugin-handle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded =
      Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded))
      << takeErrorMessage(Loaded.takeError());

  auto FirstSession = createSession(Services);
  auto OtherSession = createSession(Services);
  ASSERT_NE(FirstSession, nullptr);
  ASSERT_NE(OtherSession, nullptr);
  auto FirstTask =
      FirstSession->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  auto SiblingTask =
      FirstSession->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  auto OtherTask =
      OtherSession->createTask(NEVERC_TASK_TRANSLATION_UNIT);
  ASSERT_TRUE(static_cast<bool>(FirstTask));
  ASSERT_TRUE(static_cast<bool>(SiblingTask));
  ASSERT_TRUE(static_cast<bool>(OtherTask));

  int Destroyed = 0;
  int Payload = 17;
  auto Handle = (*FirstTask)->handles().create(
      PluginExtensionHandleKind, &Payload,
      [&](void *Pointer) {
        EXPECT_EQ(Pointer, &Payload);
        ++Destroyed;
      });
  ASSERT_TRUE(static_cast<bool>(Handle))
      << takeErrorMessage(Handle.takeError());

  void *Resolved = nullptr;
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve(*Handle, PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Resolved, &Payload);
  EXPECT_EQ((*SiblingTask)
                ->handles()
                .resolve(*Handle, PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_WRONG_SCOPE);
  EXPECT_EQ((*OtherTask)
                ->handles()
                .resolve(*Handle, PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_WRONG_SESSION);
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve(*Handle, PluginArtifactHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_WRONG_TYPE);
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve({Handle->Owner, 0}, PluginExtensionHandleKind,
                         &Resolved)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve({}, PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_INVALID_ARGUMENT);

  NevercHandle Stale = *Handle;
  EXPECT_EQ((*FirstTask)
                ->handles()
                .release(*Handle, PluginExtensionHandleKind)
                .Code,
            NEVERC_STATUS_OK);
  EXPECT_EQ(Destroyed, 1);
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve(Stale, PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  int Replacement = 23;
  auto ReplacementHandle = (*FirstTask)->handles().create(
      PluginExtensionHandleKind, &Replacement,
      [&](void *) { ++Destroyed; });
  ASSERT_TRUE(static_cast<bool>(ReplacementHandle));
  EXPECT_EQ(ReplacementHandle->Owner, Stale.Owner);
  EXPECT_NE(ReplacementHandle->Value, Stale.Value);
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve(Stale, PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  EXPECT_FALSE((*FirstTask)->end());
  EXPECT_EQ(Destroyed, 2);
  EXPECT_EQ((*FirstTask)
                ->handles()
                .resolve(*ReplacementHandle,
                         PluginExtensionHandleKind, &Resolved)
                .Code,
            NEVERC_STATUS_STALE_HANDLE);

  EXPECT_FALSE((*SiblingTask)->end());
  EXPECT_FALSE((*OtherTask)->end());
  EXPECT_FALSE(FirstSession->end());
  EXPECT_FALSE(OtherSession->end());
  EXPECT_FALSE(Services.shutdown());
}

TEST(PluginHandleArenaTest, RetiresGenerationInsteadOfWrapping) {
  PluginProcessServices Services("neverc-plugin-handle-tests",
                                 LLVM_VERSION_MAJOR);
  ASSERT_FALSE(Services.interfaces().freeze());
  auto Loaded =
      Services.registry().load(NEVERC_TEST_SCOPE_SESSION_PLUGIN);
  ASSERT_TRUE(static_cast<bool>(Loaded));
  auto Session = createSession(Services);
  ASSERT_NE(Session, nullptr);

  int Payload = 1;
  int Destroyed = 0;
  PluginHandleArena Arena(Services, Session->handle().Owner,
                          Session->handle().Owner,
                          std::numeric_limits<uint16_t>::max(), 1);
  auto LastGeneration = Arena.create(
      PluginExtensionHandleKind, &Payload,
      [&](void *) { ++Destroyed; });
  ASSERT_TRUE(static_cast<bool>(LastGeneration));
  EXPECT_EQ(
      Arena.release(*LastGeneration, PluginExtensionHandleKind).Code,
      NEVERC_STATUS_OK);
  EXPECT_EQ(Arena.retiredSlotCount(), 1U);
  EXPECT_EQ(Destroyed, 1);

  auto Exhausted =
      Arena.create(PluginExtensionHandleKind, &Payload);
  ASSERT_FALSE(static_cast<bool>(Exhausted));
  EXPECT_NE(takeErrorMessage(Exhausted.takeError()).find("exhausted"),
            std::string::npos);

  EXPECT_FALSE(Session->end());
  EXPECT_FALSE(Services.shutdown());
}

} // namespace
