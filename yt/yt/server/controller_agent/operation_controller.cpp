#include "operation_controller.h"
#include "operation_controller_host.h"
#include "config.h"
#include "helpers.h"
#include "operation.h"
#include "memory_tag_queue.h"

#include <yt/yt/server/controller_agent/controllers/ordered_controller.h>
#include <yt/yt/server/controller_agent/controllers/sort_controller.h>
#include <yt/yt/server/controller_agent/controllers/sorted_controller.h>
#include <yt/yt/server/controller_agent/controllers/unordered_controller.h>
#include <yt/yt/server/controller_agent/controllers/vanilla_controller.h>

#include <yt/yt/ytlib/object_client/public.h>

#include <yt/yt/ytlib/scheduler/config.h>
#include <yt/yt/ytlib/scheduler/job_resources_helpers.h>
#include <yt/yt/ytlib/scheduler/proto/job.pb.h>

#include <yt/yt/core/tracing/trace_context.h>

#include <yt/yt/core/profiling/timing.h>

#include <yt/yt/core/yson/consumer.h>
#include <yt/yt/core/yson/string.h>

namespace NYT::NControllerAgent {

using namespace NApi;
using namespace NScheduler;
using namespace NObjectClient;
using namespace NProfiling;
using namespace NTracing;
using namespace NYson;
using namespace NYPath;
using namespace NYTree;
using namespace NYTAlloc;

using NYT::FromProto;
using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TControllerTransactionIds* transactionIdsProto, const NControllerAgent::TControllerTransactionIds& transactionIds)
{
    ToProto(transactionIdsProto->mutable_async_id(), transactionIds.AsyncId);
    ToProto(transactionIdsProto->mutable_input_id(), transactionIds.InputId);
    ToProto(transactionIdsProto->mutable_output_id(), transactionIds.OutputId);
    ToProto(transactionIdsProto->mutable_debug_id(), transactionIds.DebugId);
    ToProto(transactionIdsProto->mutable_output_completion_id(), transactionIds.OutputCompletionId);
    ToProto(transactionIdsProto->mutable_debug_completion_id(), transactionIds.DebugCompletionId);
    ToProto(transactionIdsProto->mutable_nested_input_ids(), transactionIds.NestedInputIds);
}

void FromProto(NControllerAgent::TControllerTransactionIds* transactionIds, const NProto::TControllerTransactionIds& transactionIdsProto)
{
    transactionIds->AsyncId = FromProto<TTransactionId>(transactionIdsProto.async_id());
    transactionIds->InputId = FromProto<TTransactionId>(transactionIdsProto.input_id());
    transactionIds->OutputId = FromProto<TTransactionId>(transactionIdsProto.output_id());
    transactionIds->DebugId  = FromProto<TTransactionId>(transactionIdsProto.debug_id());
    transactionIds->OutputCompletionId = FromProto<TTransactionId>(transactionIdsProto.output_completion_id());
    transactionIds->DebugCompletionId = FromProto<TTransactionId>(transactionIdsProto.debug_completion_id());
    transactionIds->NestedInputIds = FromProto<std::vector<TTransactionId>>(transactionIdsProto.nested_input_ids());
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TInitializeOperationResult* resultProto, const TOperationControllerInitializeResult& result)
{
    resultProto->set_mutable_attributes(result.Attributes.Mutable.ToString());
    resultProto->set_brief_spec(result.Attributes.BriefSpec.ToString());
    resultProto->set_full_spec(result.Attributes.FullSpec.ToString());
    resultProto->set_unrecognized_spec(result.Attributes.UnrecognizedSpec.ToString());
    ToProto(resultProto->mutable_transaction_ids(), result.TransactionIds);
    resultProto->set_erase_offloading_trees(result.EraseOffloadingTrees);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TPrepareOperationResult* resultProto, const TOperationControllerPrepareResult& result)
{
    if (result.Attributes) {
        resultProto->set_attributes(result.Attributes.ToString());
    }
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TMaterializeOperationResult* resultProto, const TOperationControllerMaterializeResult& result)
{
    resultProto->set_suspend(result.Suspend);
    ToProto(resultProto->mutable_initial_composite_needed_resources(), result.InitialNeededResources);
    ToProto(resultProto->mutable_initial_min_needed_resources(), result.InitialMinNeededResources);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TReviveOperationResult* resultProto, const TOperationControllerReviveResult& result)
{
    resultProto->set_attributes(result.Attributes.ToString());
    resultProto->set_revived_from_snapshot(result.RevivedFromSnapshot);
    for (const auto& job : result.RevivedJobs) {
        auto* jobProto = resultProto->add_revived_jobs();
        ToProto(jobProto->mutable_job_id(), job.JobId);
        jobProto->set_start_time(ToProto<ui64>(job.StartTime));
        ToProto(jobProto->mutable_resource_limits(), job.ResourceLimits);
        ToProto(jobProto->mutable_disk_quota(), job.DiskQuota);
        jobProto->set_interruptible(job.Interruptible);
        jobProto->set_tree_id(job.TreeId);
        jobProto->set_node_id(ToProto<ui32>(job.NodeId));
        jobProto->set_node_address(job.NodeAddress);
    }
    ToProto(resultProto->mutable_revived_banned_tree_ids(), result.RevivedBannedTreeIds);
    ToProto(resultProto->mutable_composite_needed_resources(), result.NeededResources);
    ToProto(resultProto->mutable_min_needed_resources(), result.MinNeededResources);
    ToProto(resultProto->mutable_initial_min_needed_resources(), result.InitialMinNeededResources);
    resultProto->set_controller_epoch(result.ControllerEpoch);
}

////////////////////////////////////////////////////////////////////////////////

void ToProto(NProto::TCommitOperationResult* /* resultProto */, const TOperationControllerCommitResult& /* result */)
{ }

////////////////////////////////////////////////////////////////////////////////

void ToProto(
    NScheduler::NProto::TAgentToSchedulerRunningJobStatistics* jobStatisticsProto,
    const TAgentToSchedulerRunningJobStatistics& jobStatistics)
{
    ToProto(jobStatisticsProto->mutable_job_id(), jobStatistics.JobId);

    jobStatisticsProto->set_preemptible_progress_time(ToProto<i64>(jobStatistics.PreemptibleProgressTime));
}

////////////////////////////////////////////////////////////////////////////////

//! Ensures that operation controllers are being destroyed in a
//! dedicated invoker and releases memory tag when controller is destroyed.
class TOperationControllerWrapper
    : public IOperationController
{
public:
    TOperationControllerWrapper(
        TOperationId id,
        IOperationControllerPtr underlying,
        IInvokerPtr dtorInvoker,
        TMemoryTag memoryTag,
        TMemoryTagQueue* memoryTagQueue)
        : Id_(id)
        , Underlying_(std::move(underlying))
        , DtorInvoker_(std::move(dtorInvoker))
        , MemoryTag_(memoryTag)
        , TraceContext_(CreateTraceContextFromCurrent("TOperationControllerWrapper"))
        , TraceContextFinishGuard_(TraceContext_)
        , MemoryTagQueue_(memoryTagQueue)
    {
        TraceContext_->SetAllocationTagsPtr(
            New<TAllocationTags>(
                TAllocationTags::TTags({{MemoryTagLiteral, ToString(MemoryTag_)}, {OperationIdAllocationTag, ToString(Id_)}})));
    }

    ~TOperationControllerWrapper() override
    {
        auto Logger = ControllerLogger.WithTag("OperationId: %v", Id_);

        YT_LOG_INFO("Controller wrapper destructed, controller destruction scheduled (MemoryUsage: %v)",
            GetMemoryUsageForTag(MemoryTag_));

        DtorInvoker_->Invoke(BIND([
            underlying = std::move(Underlying_),
            memoryTagQueue = MemoryTagQueue_,
            memoryTag = MemoryTag_,
            Logger] () mutable
        {
            NProfiling::TWallTimer timer;
            auto memoryUsageBefore = GetMemoryUsageForTag(memoryTag);
            YT_LOG_INFO("Started destructing operation controller (MemoryUsageBefore: %v)", memoryUsageBefore);
            if (auto refCount = ResetAndGetResidualRefCount(underlying)) {
                YT_LOG_WARNING(
                    "Controller is going to be removed, but it has residual reference count; memory leak is possible "
                    "(RefCount: %v)",
                    refCount);
            }
            auto memoryUsageAfter = GetMemoryUsageForTag(memoryTag);
            YT_LOG_INFO("Finished destructing operation controller (Elapsed: %v, MemoryUsageAfter: %v, MemoryUsageDecrease: %v)",
                timer.GetElapsedTime(),
                memoryUsageAfter,
                memoryUsageBefore - memoryUsageAfter);
            if (memoryTagQueue) {
                memoryTagQueue->ReclaimTag(memoryTag);
            }
        }));
    }

    std::pair<NApi::ITransactionPtr, TString> GetIntermediateMediumTransaction() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetIntermediateMediumTransaction();
    }

    void UpdateIntermediateMediumUsage(i64 usage) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->UpdateIntermediateMediumUsage(usage);
    }

    TOperationControllerInitializeResult InitializeClean() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->InitializeClean();
    }

    TOperationControllerInitializeResult InitializeReviving(const TControllerTransactionIds& transactions) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->InitializeReviving(transactions);
    }

    TOperationControllerPrepareResult Prepare() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->Prepare();
    }

    TOperationControllerMaterializeResult Materialize() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->Materialize();
    }

    void Commit() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->Commit();
    }

    void SaveSnapshot(IZeroCopyOutput* output) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->SaveSnapshot(output);
    }

    TOperationControllerReviveResult Revive() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->Revive();
    }

    void Terminate(EControllerState finalState) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->Terminate(finalState);
    }

    void Cancel() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->Cancel();
    }

    void Complete() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->Complete();
    }

    void Dispose() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->Dispose();
    }

    bool IsThrottling() const noexcept override
    {
        return Underlying_->IsThrottling();
    }

    void RecordScheduleJobFailure(EScheduleJobFailReason reason) noexcept override
    {
        Underlying_->RecordScheduleJobFailure(reason);
    }

    void UpdateRuntimeParameters(const TOperationRuntimeParametersUpdatePtr& update) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->UpdateRuntimeParameters(update);
    }

    void OnTransactionsAborted(const std::vector<TTransactionId>& transactionIds) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->OnTransactionsAborted(transactionIds);
    }

    TCancelableContextPtr GetCancelableContext() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetCancelableContext();
    }

    IInvokerPtr GetInvoker(EOperationControllerQueue queue) const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetInvoker(queue);
    }

    IInvokerPtr GetCancelableInvoker(EOperationControllerQueue queue) const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetCancelableInvoker(queue);
    }

    IDiagnosableInvokerPool::TInvokerStatistics GetInvokerStatistics(EOperationControllerQueue queue) const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetInvokerStatistics(queue);
    }

    TFuture<void> Suspend() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->Suspend();
    }

    void Resume() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->Resume();
    }

    TCompositePendingJobCount GetPendingJobCount() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetPendingJobCount();
    }

    i64 GetFailedJobCount() const override
    {
        return Underlying_->GetFailedJobCount();
    }

    bool ShouldUpdateLightOperationAttributes() const override
    {
        return Underlying_->ShouldUpdateLightOperationAttributes();
    }

    void SetLightOperationAttributesUpdated() override
    {
        Underlying_->SetLightOperationAttributesUpdated();
    }

    bool IsRunning() const override
    {
        return Underlying_->IsRunning();
    }

    TCompositeNeededResources GetNeededResources() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetNeededResources();
    }

    void UpdateMinNeededJobResources() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->UpdateMinNeededJobResources();
    }

    TJobResourcesWithQuotaList GetMinNeededJobResources() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetMinNeededJobResources();
    }

    void OnJobAbortedEventReceivedFromScheduler(TAbortedBySchedulerJobSummary&& eventSummary) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->OnJobAbortedEventReceivedFromScheduler(std::move(eventSummary));
    }

    void OnJobRunning(std::unique_ptr<TRunningJobSummary> jobSummary) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->OnJobRunning(std::move(jobSummary));
    }

    void AbandonJob(TJobId jobId) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->AbandonJob(jobId);
    }

    void OnJobInfoReceivedFromNode(std::unique_ptr<TJobSummary> jobSummary) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->OnJobInfoReceivedFromNode(std::move(jobSummary));
    }

    void AbortJobByJobTracker(TJobId jobId, EAbortReason abortReason) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->AbortJobByJobTracker(jobId, abortReason);
    }

    TControllerScheduleJobResultPtr ScheduleJob(
        ISchedulingContext* context,
        const TJobResources& jobLimits,
        const TString& treeId) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->ScheduleJob(context, jobLimits, treeId);
    }

    void UpdateConfig(const TControllerAgentConfigPtr& config) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->UpdateConfig(config);
    }

    bool ShouldUpdateProgressAttributes() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->ShouldUpdateProgressAttributes();
    }

    void SetProgressAttributesUpdated() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        Underlying_->SetProgressAttributesUpdated();
    }

    bool HasProgress() const override
    {
        return Underlying_->HasProgress();
    }

    TYsonString GetProgress() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetProgress();
    }

    TYsonString GetBriefProgress() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetBriefProgress();
    }

    TYsonString BuildJobYson(TJobId jobId, bool outputStatistics) const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->BuildJobYson(jobId, outputStatistics);
    }

    TJobStartInfo SettleJob(TAllocationId allocationId) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->SettleJob(allocationId);
    }

    TOperationJobMetrics PullJobMetricsDelta(bool force) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->PullJobMetricsDelta(force);
    }

    TOperationAlertMap GetAlerts() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetAlerts();
    }

    TOperationInfo BuildOperationInfo() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->BuildOperationInfo();
    }

    TYsonString GetSuspiciousJobsYson() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetSuspiciousJobsYson();
    }

    TSnapshotCookie OnSnapshotStarted() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->OnSnapshotStarted();
    }

    void OnSnapshotCompleted(const TSnapshotCookie& cookie) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->OnSnapshotCompleted(cookie);
    }

    bool HasSnapshot() const override
    {
        return Underlying_->HasSnapshot();
    }

    IYPathServicePtr GetOrchid() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetOrchid();
    }

    void ZombifyOrchid() override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->ZombifyOrchid();
    }

    TString WriteCoreDump() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->WriteCoreDump();
    }

    void RegisterOutputRows(i64 count, int tableIndex) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->RegisterOutputRows(count, tableIndex);
    }

    std::optional<int> GetRowCountLimitTableIndex() override
    {
        return Underlying_->GetRowCountLimitTableIndex();
    }

    void LoadSnapshot(const TOperationSnapshot& snapshot) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->LoadSnapshot(snapshot);
    }

    i64 GetMemoryUsage() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->GetMemoryUsage();
    }

    void SetOperationAlert(EOperationAlertType type, const TError& alert) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->SetOperationAlert(type, alert);
    }

    void OnMemoryLimitExceeded(const TError& error) override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->OnMemoryLimitExceeded(error);
    }

    bool IsMemoryLimitExceeded() const override
    {
        auto guard = TCurrentTraceContextGuard(TraceContext_);

        return Underlying_->IsMemoryLimitExceeded();
    }

    bool IsFinished() const override
    {
        return Underlying_->IsFinished();
    }

private:

    const TOperationId Id_;
    const IOperationControllerPtr Underlying_;
    const IInvokerPtr DtorInvoker_;
    const TMemoryTag MemoryTag_;

    const TTraceContextPtr TraceContext_;
    const TTraceContextFinishGuard TraceContextFinishGuard_;

    TMemoryTagQueue* const MemoryTagQueue_;
};

////////////////////////////////////////////////////////////////////////////////

void ApplyPatch(
    const TYPath& path,
    const INodePtr& root,
    const INodePtr& templatePatch,
    const INodePtr& patch)
{
    auto node = FindNodeByYPath(root, path);
    if (node) {
        node = CloneNode(node);
    }
    if (templatePatch) {
        if (node) {
            node = PatchNode(templatePatch, node);
        } else {
            node = templatePatch;
        }
    }
    if (patch) {
        if (node) {
            node = PatchNode(node, patch);
        } else {
            node = patch;
        }
    }
    if (node) {
        ForceYPath(root, path);
        // Note that #node may be equal to one of the #root's subtrees or to one of the patches.
        // In any case, we do not want to use it as an argument to SetNodeByYPath, since this wonderful
        // method would change the parent of the argument node, which may lead to child-parent relation inconsistency.
        SetNodeByYPath(root, path, CloneNode(node));
    }
}

void ApplyExperiments(TOperation* operation)
{
    const auto& spec = operation->GetSpec();
    std::vector<TYPath> userJobPaths;
    std::vector<TYPath> jobIOPaths;
    jobIOPaths.push_back("/auto_merge/job_io");
    switch (operation->GetType()) {
        case EOperationType::Map: {
            userJobPaths.push_back("/mapper");
            jobIOPaths.push_back("/job_io");
            break;
        }
        case EOperationType::JoinReduce:
        case EOperationType::Reduce: {
            userJobPaths.push_back("/reducer");
            jobIOPaths.push_back("/job_io");
            break;
        }
        case EOperationType::MapReduce: {
            if (FindNodeByYPath(spec, "/mapper")) {
                userJobPaths.push_back("/mapper");
            }
            if (FindNodeByYPath(spec, "/reduce_combiner")) {
                userJobPaths.push_back("/reduce_combiner");
            }
            userJobPaths.push_back("/reducer");
            jobIOPaths.push_back("/map_job_io");
            jobIOPaths.push_back("/sort_job_io");
            jobIOPaths.push_back("/reduce_job_io");
            break;
        }
        case EOperationType::Sort: {
            jobIOPaths.push_back("/partition_job_io");
            jobIOPaths.push_back("/sort_job_io");
            jobIOPaths.push_back("/merge_job_io");
            break;
        }
        case EOperationType::Merge:
        case EOperationType::Erase:
        case EOperationType::RemoteCopy: {
            jobIOPaths.push_back("/job_io");
            break;
        }
        case EOperationType::Vanilla: {
            auto tasks = GetNodeByYPath(spec, "/tasks");
            for (const auto& key : tasks->AsMap()->GetKeys()) {
                userJobPaths.push_back("/tasks/" + key);
                jobIOPaths.push_back("/tasks/" + key + "/job_io");
            }
            break;
        }
    }

    for (const auto& experiment : operation->ExperimentAssignments()) {
        for (const auto& path : userJobPaths) {
            ApplyPatch(
                path,
                spec,
                experiment->Effect->ControllerUserJobSpecTemplatePatch,
                experiment->Effect->ControllerUserJobSpecPatch);
        }
        for (const auto& path : jobIOPaths) {
            ApplyPatch(
                path,
                spec,
                experiment->Effect->ControllerJobIOTemplatePatch,
                experiment->Effect->ControllerJobIOPatch);
        }
    }
}

IOperationControllerPtr CreateControllerForOperation(
    TControllerAgentConfigPtr config,
    TOperation* operation)
{
    IOperationControllerPtr controller;
    auto host = operation->GetHost();
    ApplyExperiments(operation);
    switch (operation->GetType()) {
        case EOperationType::Map: {
            auto baseSpec = ParseOperationSpec<TMapOperationSpec>(operation->GetSpec());
            controller = baseSpec->Ordered
                ? NControllers::CreateOrderedMapController(config, host, operation)
                : NControllers::CreateUnorderedMapController(config, host, operation);
            break;
        }
        case EOperationType::Merge: {
            auto baseSpec = ParseOperationSpec<TMergeOperationSpec>(operation->GetSpec());
            switch (baseSpec->Mode) {
                case EMergeMode::Ordered: {
                    controller = NControllers::CreateOrderedMergeController(config, host, operation);
                    break;
                }
                case EMergeMode::Sorted: {
                    controller = NControllers::CreateSortedMergeController(config, host, operation);
                    break;
                }
                case EMergeMode::Unordered: {
                    controller = NControllers::CreateUnorderedMergeController(config, host, operation);
                    break;
                }
            }
            break;
        }
        case EOperationType::Erase: {
            controller = NControllers::CreateEraseController(config, host, operation);
            break;
        }
        case EOperationType::Sort: {
            controller = NControllers::CreateSortController(config, host, operation);
            break;
        }
        case EOperationType::Reduce: {
            controller = NControllers::CreateReduceController(config, host, operation, /* isJoinReduce */ false);
            break;
        }
        case EOperationType::JoinReduce: {
            controller = NControllers::CreateReduceController(config, host, operation, /* isJoinReduce */ true);
            break;
        }
        case EOperationType::MapReduce: {
            controller = NControllers::CreateMapReduceController(config, host, operation);
            break;
        }
        case EOperationType::RemoteCopy: {
            controller = NControllers::CreateRemoteCopyController(config, host, operation);
            break;
        }
        case EOperationType::Vanilla: {
            controller = NControllers::CreateVanillaController(config, host, operation);
            break;
        }
        default:
            YT_ABORT();
    }

    return New<TOperationControllerWrapper>(
        operation->GetId(),
        controller,
        controller->GetInvoker(),
        operation->GetMemoryTag(),
        host->GetMemoryTagQueue());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NControllerAgent
