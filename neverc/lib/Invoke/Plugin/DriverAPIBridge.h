#ifndef NEVERC_INVOKE_PLUGIN_DRIVERAPIBRIDGE_H
#define NEVERC_INVOKE_PLUGIN_DRIVERAPIBRIDGE_H

#include "neverc/Plugin/Host/PluginInterfaceRegistry.h"
#include "neverc/Plugin/PluginDriver.h"
#include "llvm/Support/Error.h"
#include <mutex>

namespace neverc::plugin {
class PluginPhaseExecutor;
class PluginTaskContext;
} // namespace neverc::plugin

namespace neverc::driver {

class DriverAPIBridge {
public:
  DriverAPIBridge();

  llvm::Error registerInterface(plugin::PluginInterfaceRegistry &Interfaces);
  llvm::Error bind(plugin::PluginPhaseExecutor &Executor,
                   plugin::PluginTaskContext &Task);
  void unbind();

private:
  NevercDriverAPI API{};
  std::mutex Mutex;
  plugin::PluginPhaseExecutor *ActiveExecutor = nullptr;
  plugin::PluginTaskContext *ActiveTask = nullptr;

  static NevercStatus NEVERC_CALL
  getArgumentCount(void *Context, const NevercPhaseFrame *Frame,
                   NevercArtifactHandle Arguments, uint64_t *OutCount);
  static NevercStatus NEVERC_CALL
  getArgument(void *Context, const NevercPhaseFrame *Frame,
              NevercArtifactHandle Arguments, uint64_t Index,
              NevercStringView *OutValue, NevercArgumentOrigin *OutOrigin,
              NevercStringView *OutSource, uint64_t *OutPosition);
  static NevercStatus NEVERC_CALL beginArgumentMutation(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation, NevercArtifactHandle Arguments,
      NevercArgumentMutationHandle *OutMutation);
  static NevercStatus NEVERC_CALL
  insertArgument(void *Context, NevercArgumentMutationHandle Mutation,
                 uint64_t Index, NevercStringView Value);
  static NevercStatus NEVERC_CALL
  replaceArgument(void *Context, NevercArgumentMutationHandle Mutation,
                  uint64_t Index, NevercStringView Value);
  static NevercStatus NEVERC_CALL eraseArgument(
      void *Context, NevercArgumentMutationHandle Mutation, uint64_t Index);
  static NevercStatus NEVERC_CALL
  commitArgumentMutation(void *Context, NevercArgumentMutationHandle Mutation);
  static NevercStatus NEVERC_CALL
  abortArgumentMutation(void *Context, NevercArgumentMutationHandle Mutation);
  static NevercStatus NEVERC_CALL
  getOptionOccurrenceCount(void *Context, const NevercPhaseFrame *Frame,
                           NevercArtifactHandle Arguments, uint64_t *OutCount);
  static NevercStatus NEVERC_CALL
  getOptionOccurrence(void *Context, const NevercPhaseFrame *Frame,
                      NevercArtifactHandle Arguments, uint64_t Index,
                      NevercOptionOccurrence *OutOccurrence);
  static NevercStatus NEVERC_CALL beginParsedArgumentMutation(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation, NevercArtifactHandle Arguments,
      NevercParsedArgumentMutationHandle *OutMutation);
  static NevercStatus NEVERC_CALL addOptionOccurrence(
      void *Context, NevercParsedArgumentMutationHandle Mutation,
      NevercStringView Spelling, NevercStringList Values);
  static NevercStatus NEVERC_CALL removeOptionOccurrence(
      void *Context, NevercParsedArgumentMutationHandle Mutation,
      uint64_t Occurrence);
  static NevercStatus NEVERC_CALL replaceOptionOccurrence(
      void *Context, NevercParsedArgumentMutationHandle Mutation,
      uint64_t Occurrence, NevercStringView Spelling, NevercStringList Values);
  static NevercStatus NEVERC_CALL commitParsedArgumentMutation(
      void *Context, NevercParsedArgumentMutationHandle Mutation);
  static NevercStatus NEVERC_CALL abortParsedArgumentMutation(
      void *Context, NevercParsedArgumentMutationHandle Mutation);
  static NevercStatus NEVERC_CALL getToolChainRequest(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Request, NevercToolChainRequest *OutRequest);
  static NevercStatus NEVERC_CALL beginToolChainMutation(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation, NevercArtifactHandle Request,
      NevercToolChainMutationHandle *OutMutation);
  static NevercStatus NEVERC_CALL
  setToolChainTriple(void *Context, NevercToolChainMutationHandle Mutation,
                     NevercStringView Triple);
  static NevercStatus NEVERC_CALL
  setToolChainCPU(void *Context, NevercToolChainMutationHandle Mutation,
                  NevercStringView CPU);
  static NevercStatus NEVERC_CALL
  setToolChainFeatures(void *Context, NevercToolChainMutationHandle Mutation,
                       NevercStringList Features);
  static NevercStatus NEVERC_CALL commitToolChainMutation(
      void *Context, NevercToolChainMutationHandle Mutation);
  static NevercStatus NEVERC_CALL
  abortToolChainMutation(void *Context, NevercToolChainMutationHandle Mutation);
  static NevercStatus NEVERC_CALL
  createToolChainSelection(void *Context, const NevercPhaseFrame *Frame,
                           NevercArtifactHandle Request,
                           const NevercToolChainSelectionDescriptor *Descriptor,
                           NevercArtifactHandle *OutSelection);
  static NevercStatus NEVERC_CALL getToolChainSelection(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Selection, NevercToolChainSelection *OutSelection);
  static NevercStatus NEVERC_CALL getDriverInputCount(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Request, uint64_t *OutCount);
  static NevercStatus NEVERC_CALL
  getDriverInput(void *Context, const NevercPhaseFrame *Frame,
                 NevercArtifactHandle Request, uint64_t Index,
                 NevercDriverInput *OutInput);
  static NevercStatus NEVERC_CALL getActionNodeCount(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      uint64_t *OutCount);
  static NevercStatus NEVERC_CALL
  getActionNode(void *Context, const NevercPhaseFrame *Frame,
                NevercArtifactHandle Graph, uint64_t Index,
                NevercActionNode *OutNode);
  static NevercStatus NEVERC_CALL getActionNodeInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      NevercActionNodeID Node, uint64_t Index,
      NevercActionNodeID *OutInput);
  static NevercStatus NEVERC_CALL getActionRootCount(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      uint64_t *OutCount);
  static NevercStatus NEVERC_CALL
  getActionRoot(void *Context, const NevercPhaseFrame *Frame,
                NevercArtifactHandle Graph, uint64_t Index,
                NevercActionNodeID *OutRoot);
  static NevercStatus NEVERC_CALL createActionGraphBuilder(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Request, NevercActionGraphBuilderHandle *OutBuilder);
  static NevercStatus NEVERC_CALL beginActionGraphMutation(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation, NevercArtifactHandle Graph,
      NevercActionGraphMutationHandle *OutMutation);
  static NevercStatus NEVERC_CALL addActionNode(
      void *Context, NevercActionGraphBuilderHandle Builder,
      const NevercActionNodeDescriptor *Descriptor,
      NevercActionNodeID *OutNode);
  static NevercStatus NEVERC_CALL removeActionNode(
      void *Context, NevercActionGraphBuilderHandle Builder,
      NevercActionNodeID Node);
  static NevercStatus NEVERC_CALL replaceActionNodeInputs(
      void *Context, NevercActionGraphBuilderHandle Builder,
      NevercActionNodeID Node, NevercActionNodeIDList Inputs);
  static NevercStatus NEVERC_CALL setActionNodeOutputType(
      void *Context, NevercActionGraphBuilderHandle Builder,
      NevercActionNodeID Node, NevercDriverType OutputType);
  static NevercStatus NEVERC_CALL setActionNodeBindArch(
      void *Context, NevercActionGraphBuilderHandle Builder,
      NevercActionNodeID Node, NevercStringView BindArch);
  static NevercStatus NEVERC_CALL setActionRoots(
      void *Context, NevercActionGraphBuilderHandle Builder,
      NevercActionNodeIDList Roots);
  static NevercStatus NEVERC_CALL publishActionGraph(
      void *Context, const NevercPhaseFrame *Frame,
      NevercActionGraphBuilderHandle Builder,
      NevercArtifactHandle *OutGraph);
  static NevercStatus NEVERC_CALL commitActionGraphMutation(
      void *Context, NevercActionGraphMutationHandle Mutation);
  static NevercStatus NEVERC_CALL
  abortActionGraphEdit(void *Context, NevercHandle Edit);
  static NevercStatus NEVERC_CALL getJobCount(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      uint64_t *OutCount);
  static NevercStatus NEVERC_CALL
  getJob(void *Context, const NevercPhaseFrame *Frame,
         NevercArtifactHandle Graph, uint64_t Index, NevercJob *OutJob);
  static NevercStatus NEVERC_CALL getJobDependency(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      NevercJobID Job, uint64_t Index, NevercJobID *OutDependency);
  static NevercStatus NEVERC_CALL getJobArgument(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      NevercJobID Job, uint64_t Index, NevercStringView *OutValue);
  static NevercStatus NEVERC_CALL getJobEnvironment(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      NevercJobID Job, uint64_t Index, NevercStringView *OutValue);
  static NevercStatus NEVERC_CALL getJobInput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      NevercJobID Job, uint64_t Index, NevercJobFile *OutFile);
  static NevercStatus NEVERC_CALL getJobOutput(
      void *Context, const NevercPhaseFrame *Frame, NevercArtifactHandle Graph,
      NevercJobID Job, uint64_t Index, NevercJobFile *OutFile);
  static NevercStatus NEVERC_CALL createJobGraphBuilder(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle ActionGraph,
      NevercJobGraphBuilderHandle *OutBuilder);
  static NevercStatus NEVERC_CALL beginJobGraphMutation(
      void *Context, const NevercPhaseFrame *Frame,
      NevercPhaseContinuation *Continuation, NevercArtifactHandle Graph,
      NevercJobGraphMutationHandle *OutMutation);
  static NevercStatus NEVERC_CALL addJob(
      void *Context, NevercHandle Edit,
      const NevercJobDescriptor *Descriptor, NevercJobID *OutJob);
  static NevercStatus NEVERC_CALL removeJob(
      void *Context, NevercHandle Edit, NevercJobID Job);
  static NevercStatus NEVERC_CALL moveJobBefore(
      void *Context, NevercHandle Edit, NevercJobID Job, NevercJobID Before);
  static NevercStatus NEVERC_CALL replaceJob(
      void *Context, NevercHandle Edit, NevercJobID Job,
      const NevercJobDescriptor *Descriptor);
  static NevercStatus NEVERC_CALL setJobArgument(
      void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
      NevercStringView Value);
  static NevercStatus NEVERC_CALL setJobEnvironment(
      void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
      NevercStringView Value);
  static NevercStatus NEVERC_CALL setJobInput(
      void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
      const NevercJobFile *File);
  static NevercStatus NEVERC_CALL setJobOutput(
      void *Context, NevercHandle Edit, NevercJobID Job, uint64_t Index,
      const NevercJobFile *File);
  static NevercStatus NEVERC_CALL replaceJobDependencies(
      void *Context, NevercHandle Edit, NevercJobID Job,
      NevercJobIDList Dependencies);
  static NevercStatus NEVERC_CALL publishJobGraph(
      void *Context, const NevercPhaseFrame *Frame,
      NevercJobGraphBuilderHandle Builder, NevercArtifactHandle *OutGraph);
  static NevercStatus NEVERC_CALL commitJobGraphMutation(
      void *Context, NevercJobGraphMutationHandle Mutation);
  static NevercStatus NEVERC_CALL
  abortJobGraphEdit(void *Context, NevercHandle Edit);
  static NevercStatus NEVERC_CALL getJobExecutionRequest(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Request,
      NevercJobExecutionRequest *OutRequest);
  static NevercStatus NEVERC_CALL createJobResult(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Request,
      const NevercJobResultDescriptor *Descriptor,
      NevercArtifactHandle *OutResult);
  static NevercStatus NEVERC_CALL getJobResult(
      void *Context, const NevercPhaseFrame *Frame,
      NevercArtifactHandle Result, NevercJobResult *OutResult);
};

} // namespace neverc::driver

#endif
