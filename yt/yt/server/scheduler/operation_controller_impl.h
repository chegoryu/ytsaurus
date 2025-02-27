#pragma once

#include "operation_controller.h"
#include "controller_agent.h"

#include <yt/yt/ytlib/controller_agent/controller_agent_service_proxy.h>
#include <yt/yt/ytlib/controller_agent/job_prober_service_proxy.h>

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

class TOperationControllerImpl
    : public IOperationController
{
public:
    TOperationControllerImpl(
        TBootstrap* bootstrap,
        TSchedulerConfigPtr config,
        const TOperationPtr& operation);

    void AssignAgent(const TControllerAgentPtr& agent, TControllerEpoch epoch) override;

    bool RevokeAgent() override;

    TControllerAgentPtr FindAgent() const override;

    TControllerEpoch GetEpoch() const override;

    TFuture<TOperationControllerInitializeResult> Initialize(const std::optional<TOperationTransactions>& transactions) override;
    TFuture<TOperationControllerPrepareResult> Prepare() override;
    TFuture<TOperationControllerMaterializeResult> Materialize() override;
    TFuture<TOperationControllerReviveResult> Revive() override;
    TFuture<TOperationControllerCommitResult> Commit() override;
    TFuture<void> Terminate(EOperationState finalState) override;
    TFuture<void> Complete() override;
    TFuture<void> Register(const TOperationPtr& operation) override;
    TFuture<TOperationControllerUnregisterResult> Unregister() override;
    TFuture<void> UpdateRuntimeParameters(TOperationRuntimeParametersUpdatePtr update) override;

    void OnNonscheduledJobAborted(
        TJobId jobId,
        EAbortReason abortReason,
        TControllerEpoch jobEpoch) override;
    void OnJobAborted(
        const TJobPtr& job,
        const TError& error,
        bool scheduled,
        std::optional<EAbortReason> abortReason) override;

    TFuture<void> AbandonJob(TOperationId operationId, TJobId jobId) override;

    void OnInitializationFinished(const TErrorOr<TOperationControllerInitializeResult>& resultOrError) override;
    void OnPreparationFinished(const TErrorOr<TOperationControllerPrepareResult>& resultOrError) override;
    void OnMaterializationFinished(const TErrorOr<TOperationControllerMaterializeResult>& resultOrError) override;
    void OnRevivalFinished(const TErrorOr<TOperationControllerReviveResult>& resultOrError) override;
    void OnCommitFinished(const TErrorOr<TOperationControllerCommitResult>& resultOrError) override;

    void SetControllerRuntimeData(const TControllerRuntimeDataPtr& controllerData) override;

    TFuture<void> GetFullHeartbeatProcessed() override;

    TFuture<TControllerScheduleJobResultPtr> ScheduleJob(
        const ISchedulingContextPtr& context,
        const TJobResources& jobLimits,
        const TString& treeId,
        const TString& poolPath,
        const TFairShareStrategyTreeConfigPtr& treeConfig) override;

    void UpdateMinNeededJobResources() override;

    TCompositeNeededResources GetNeededResources() const override;
    TJobResourcesWithQuotaList GetMinNeededJobResources() const override;
    TJobResourcesWithQuotaList GetInitialMinNeededJobResources() const override;
    EPreemptionMode GetPreemptionMode() const override;

    std::pair<NApi::ITransactionPtr, TString> GetIntermediateMediumTransaction();
    void UpdateIntermediateMediumUsage(i64 usage);

private:
    TBootstrap* const Bootstrap_;
    TSchedulerConfigPtr Config_;
    const TOperationId OperationId_;
    const EPreemptionMode PreemptionMode_;
    const NLogging::TLogger Logger;

    TControllerRuntimeDataPtr ControllerRuntimeData_;
    TJobResourcesWithQuotaList InitialMinNeededResources_;

    YT_DECLARE_SPIN_LOCK(NThreading::TSpinLock, SpinLock_);

    TIncarnationId IncarnationId_;
    TWeakPtr<TControllerAgent> Agent_;
    std::unique_ptr<NControllerAgent::TControllerAgentServiceProxy> ControllerAgentTrackerProxy_;
    std::unique_ptr<NControllerAgent::TJobProberServiceProxy> ControllerAgentJobProberProxy_;

    std::atomic<TControllerEpoch> Epoch_;

    TSchedulerToAgentJobEventOutboxPtr JobEventsOutbox_;
    TSchedulerToAgentOperationEventOutboxPtr OperationEventsOutbox_;
    TScheduleJobRequestOutboxPtr ScheduleJobRequestsOutbox_;

    TPromise<TOperationControllerInitializeResult> PendingInitializeResult_;
    TPromise<TOperationControllerPrepareResult> PendingPrepareResult_;
    TPromise<TOperationControllerMaterializeResult> PendingMaterializeResult_;
    TPromise<TOperationControllerReviveResult> PendingReviveResult_;
    TPromise<TOperationControllerCommitResult> PendingCommitResult_;

    DECLARE_THREAD_AFFINITY_SLOT(ControlThread);

    bool ShouldSkipJobEvent(TJobId jobId, TControllerEpoch jobEpoch) const;
    bool ShouldSkipJobEvent(const TJobPtr& job) const;

    bool EnqueueAbortedJobEvent(TAbortedBySchedulerJobSummary&& summary);
    void EnqueueOperationEvent(TSchedulerToAgentOperationEvent&& event);
    void EnqueueScheduleJobRequest(TScheduleJobRequestPtr&& event);

    // TODO(ignat): move to inl
    template <class TResponse, class TRequest>
    TFuture<TIntrusivePtr<TResponse>> InvokeAgent(
        const TIntrusivePtr<TRequest>& request);

    void ProcessControllerAgentError(const TError& error);

    void OnJobAborted(
        TJobId jobId,
        const TError& error,
        const bool scheduled,
        std::optional<EAbortReason> abortReason,
        TControllerEpoch jobEpoch);
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
