#pragma once

#include "client_common.h"

#include <yt/yt/client/job_tracker_client/public.h>

#include <yt/yt/client/chaos_client/public.h>

namespace NYT::NApi {

////////////////////////////////////////////////////////////////////////////////

struct TBuildSnapshotOptions
    : public TTimeoutOptions
{
    //! Refers either to masters or to tablet cells.
    //! If null then the primary one is assumed.
    NHydra::TCellId CellId;
    bool SetReadOnly = false;
    bool WaitForSnapshotCompletion = true;
};

struct TBuildMasterSnapshotsOptions
    : public TTimeoutOptions
{
    bool SetReadOnly = false;
    bool WaitForSnapshotCompletion = true;
    bool Retry = true;
};

struct TExitReadOnlyOptions
    : public TTimeoutOptions
{ };

struct TMasterExitReadOnlyOptions
    : public TTimeoutOptions
{
    bool Retry = true;
};

struct TSwitchLeaderOptions
    : public TTimeoutOptions
{ };

struct TResetStateHashOptions
    : public TTimeoutOptions
{
    //! If not set, random number is used.
    std::optional<ui64> NewStateHash;
};

struct TGCCollectOptions
    : public TTimeoutOptions
{
    //! Refers to master cell.
    //! If null then the primary one is assumed.
    NHydra::TCellId CellId;
};

struct TKillProcessOptions
    : public TTimeoutOptions
{
    int ExitCode = 42;
};

struct TWriteCoreDumpOptions
    : public TTimeoutOptions
{ };

struct TWriteLogBarrierOptions
    : public TTimeoutOptions
{
    TString Category;
};

struct TWriteOperationControllerCoreDumpOptions
    : public TTimeoutOptions
{ };

struct THealExecNodeOptions
    : public TTimeoutOptions
{
    std::vector<TString> Locations;
    std::vector<TString> AlertTypesToReset;
    bool ForceReset = false;
};

struct TSuspendCoordinatorOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{ };

struct TResumeCoordinatorOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{ };

struct TMigrateReplicationCardsOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{
    NObjectClient::TCellId DestinationCellId;
    std::vector<NChaosClient::TReplicationCardId> ReplicationCardIds;
};

struct TSuspendChaosCellsOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{ };

struct TResumeChaosCellsOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{ };

struct TSuspendTabletCellsOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{ };

struct TResumeTabletCellsOptions
    : public TTimeoutOptions
    , public TMutatingOptions
{ };

struct TDisableChunkLocationsOptions
    : public TTimeoutOptions
{ };

struct TDisableChunkLocationsResult
{
    std::vector<TGuid> LocationUuids;
};

struct TDestroyChunkLocationsOptions
    : public TTimeoutOptions
{ };

struct TDestroyChunkLocationsResult
{
    std::vector<TGuid> LocationUuids;
};

struct TResurrectChunkLocationsOptions
    : public TTimeoutOptions
{ };

struct TResurrectChunkLocationsResult
{
    std::vector<TGuid> LocationUuids;
};

using TCellIdToSnapshotIdMap = THashMap<NHydra::TCellId, int>;

struct TAddMaintenanceOptions
    : public TTimeoutOptions
{ };

struct TMaintenanceFilter
{
    struct TByUser
    {
        struct TAll
        { };

        struct TMine
        { };
    };

    // Empty means no filtering by id.
    std::vector<TMaintenanceId> Ids;
    std::optional<EMaintenanceType> Type;
    std::variant<TByUser::TAll, TByUser::TMine, TString> User;
};

struct TRemoveMaintenanceOptions
    : public TTimeoutOptions
{ };

struct TRequestRebootOptions
    : public TTimeoutOptions
{ };

struct TRequestRebootResult
{ };

////////////////////////////////////////////////////////////////////////////////

struct IAdminClient
{
    virtual TFuture<int> BuildSnapshot(
        const TBuildSnapshotOptions& options = {}) = 0;

    virtual TFuture<TCellIdToSnapshotIdMap> BuildMasterSnapshots(
        const TBuildMasterSnapshotsOptions& options = {}) = 0;

    virtual TFuture<void> ExitReadOnly(
        NHydra::TCellId cellId,
        const TExitReadOnlyOptions& options = {}) = 0;

    virtual TFuture<void> MasterExitReadOnly(
        const TMasterExitReadOnlyOptions& options = {}) = 0;

    virtual TFuture<void> SwitchLeader(
        NHydra::TCellId cellId,
        const TString& newLeaderAddress,
        const TSwitchLeaderOptions& options = {}) = 0;

    virtual TFuture<void> ResetStateHash(
        NHydra::TCellId cellId,
        const TResetStateHashOptions& options = {}) = 0;

    virtual TFuture<void> GCCollect(
        const TGCCollectOptions& options = {}) = 0;

    virtual TFuture<void> KillProcess(
        const TString& address,
        const TKillProcessOptions& options = {}) = 0;

    virtual TFuture<TString> WriteCoreDump(
        const TString& address,
        const TWriteCoreDumpOptions& options = {}) = 0;

    virtual TFuture<TGuid> WriteLogBarrier(
        const TString& address,
        const TWriteLogBarrierOptions& options) = 0;

    virtual TFuture<TString> WriteOperationControllerCoreDump(
        NJobTrackerClient::TOperationId operationId,
        const TWriteOperationControllerCoreDumpOptions& options = {}) = 0;

    virtual TFuture<void> HealExecNode(
        const TString& address,
        const THealExecNodeOptions& options = {}) = 0;

    virtual TFuture<void> SuspendCoordinator(
        NObjectClient::TCellId coordinatorCellId,
        const TSuspendCoordinatorOptions& options = {}) = 0;

    virtual TFuture<void> ResumeCoordinator(
        NObjectClient::TCellId coordinatorCellId,
        const TResumeCoordinatorOptions& options = {}) = 0;

    virtual TFuture<void> MigrateReplicationCards(
        NObjectClient::TCellId chaosCellId,
        const TMigrateReplicationCardsOptions& options = {}) = 0;

    virtual TFuture<void> SuspendChaosCells(
        const std::vector<NObjectClient::TCellId>& cellIds,
        const TSuspendChaosCellsOptions& options = {}) = 0;

    virtual TFuture<void> ResumeChaosCells(
        const std::vector<NObjectClient::TCellId>& cellIds,
        const TResumeChaosCellsOptions& options = {}) = 0;

    virtual TFuture<void> SuspendTabletCells(
        const std::vector<NObjectClient::TCellId>& cellIds,
        const TSuspendTabletCellsOptions& options = {}) = 0;

    virtual TFuture<void> ResumeTabletCells(
        const std::vector<NObjectClient::TCellId>& cellIds,
        const TResumeTabletCellsOptions& options = {}) = 0;

    virtual TFuture<TMaintenanceId> AddMaintenance(
        EMaintenanceComponent component,
        const TString& address,
        EMaintenanceType type,
        const TString& comment,
        const TAddMaintenanceOptions& options = {}) = 0;

    virtual TFuture<TMaintenanceCounts> RemoveMaintenance(
        EMaintenanceComponent component,
        const TString& address,
        const TMaintenanceFilter& filter,
        const TRemoveMaintenanceOptions& options = {}) = 0;

    virtual TFuture<TDisableChunkLocationsResult> DisableChunkLocations(
        const TString& nodeAddress,
        const std::vector<TGuid>& locationUuids,
        const TDisableChunkLocationsOptions& options = {}) = 0;

    virtual TFuture<TDestroyChunkLocationsResult> DestroyChunkLocations(
        const TString& nodeAddress,
        const std::vector<TGuid>& locationUuids,
        const TDestroyChunkLocationsOptions& options = {}) = 0;

    virtual TFuture<TResurrectChunkLocationsResult> ResurrectChunkLocations(
        const TString& nodeAddress,
        const std::vector<TGuid>& locationUuids,
        const TResurrectChunkLocationsOptions& options = {}) = 0;

    virtual TFuture<TRequestRebootResult> RequestReboot(
        const TString& nodeAddress,
        const TRequestRebootOptions& options = {}) = 0;
};

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi
