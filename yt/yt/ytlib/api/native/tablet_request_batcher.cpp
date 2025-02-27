#include "tablet_request_batcher.h"

#include <yt/yt/ytlib/table_client/row_merger.h>

#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/schema.h>

namespace NYT::NApi::NNative {

using namespace NTableClient;
using namespace NQueryClient;

////////////////////////////////////////////////////////////////////////////////

class TTabletRequestBatcher
    : public ITabletRequestBatcher
{
public:
    TTabletRequestBatcher(
        TTabletRequestBatcherOptions options,
        TTableSchemaPtr tableSchema,
        TColumnEvaluatorPtr columnEvaluator)
        : Options_(std::move(options))
        , TableSchema_(std::move(tableSchema))
        , ColumnEvaluator_(std::move(columnEvaluator))
    { }

    virtual void SubmitUnversionedRow(
        EWireProtocolCommand command,
        TUnversionedRow row,
        TLockMask lockMask)
    {
        auto sequentialId = std::ssize(UnversionedSubmittedRows_);
        UnversionedSubmittedRows_.push_back({
            command,
            row,
            lockMask,
            static_cast<int>(sequentialId)});
    }

    virtual void SubmitVersionedRow(TTypeErasedRow row)
    {
        VersionedSubmittedRows_.push_back(row);
    }

    virtual std::vector<std::unique_ptr<TBatch>> PrepareBatches()
    {
        if (!VersionedSubmittedRows_.empty() && !UnversionedSubmittedRows_.empty()) {
            THROW_ERROR_EXCEPTION("Cannot intermix versioned and unversioned writes to a single table "
                "within a transaction");
        }

        if (TableSchema_->IsSorted()) {
            PrepareSortedBatches();
        } else {
            PrepareOrderedBatches();
        }

        return std::move(Batches_);
    }

private:
    const TTabletRequestBatcherOptions Options_;
    const TTableSchemaPtr TableSchema_;
    const TColumnEvaluatorPtr ColumnEvaluator_;

    struct TUnversionedSubmittedRow
    {
        EWireProtocolCommand Command;
        TUnversionedRow Row;
        TLockMask Locks;
        int SequentialId;
    };
    std::vector<TUnversionedSubmittedRow> UnversionedSubmittedRows_;

    std::vector<TTypeErasedRow> VersionedSubmittedRows_;

    std::vector<std::unique_ptr<TBatch>> Batches_;

    i64 TotalRowCount_ = 0;

    void PrepareSortedBatches()
    {
        auto columnCount = TableSchema_->GetColumnCount();
        auto keyColumnCount = TableSchema_->GetKeyColumnCount();

        std::sort(
            UnversionedSubmittedRows_.begin(),
            UnversionedSubmittedRows_.end(),
            [=] (const TUnversionedSubmittedRow& lhs, const TUnversionedSubmittedRow& rhs) {
                // NB: CompareRows may throw on composite values.
                int res = CompareRows(lhs.Row, rhs.Row, keyColumnCount);
                return res != 0 ? res < 0 : lhs.SequentialId < rhs.SequentialId;
            });

        std::vector<TUnversionedSubmittedRow> unversionedMergedRows;
        unversionedMergedRows.reserve(UnversionedSubmittedRows_.size());

        struct TTabletRequestBatcherBufferTag
        { };
        auto rowBuffer = New<TRowBuffer>(TTabletRequestBatcherBufferTag());

        TUnversionedRowMerger rowMerger(
            rowBuffer,
            columnCount,
            keyColumnCount,
            ColumnEvaluator_);

        for (auto it = UnversionedSubmittedRows_.begin(); it != UnversionedSubmittedRows_.end();) {
            auto startIt = it;
            rowMerger.InitPartialRow(startIt->Row);

            TLockMask lockMask;
            EWireProtocolCommand resultCommand;

            do {
                switch (it->Command) {
                    case EWireProtocolCommand::DeleteRow:
                        rowMerger.DeletePartialRow(it->Row);
                        break;

                    case EWireProtocolCommand::WriteRow:
                        rowMerger.AddPartialRow(it->Row);
                        break;

                    case EWireProtocolCommand::WriteAndLockRow:
                        rowMerger.AddPartialRow(it->Row);
                        lockMask = MaxMask(lockMask, it->Locks);
                        break;

                    default:
                        YT_ABORT();
                }
                resultCommand = it->Command;
                ++it;
            } while (it != UnversionedSubmittedRows_.end() &&
                CompareRows(it->Row, startIt->Row, keyColumnCount) == 0);

            TUnversionedRow mergedRow;
            if (resultCommand == EWireProtocolCommand::DeleteRow) {
                mergedRow = rowMerger.BuildDeleteRow();
            } else {
                if (lockMask.GetSize() > 0) {
                    resultCommand = EWireProtocolCommand::WriteAndLockRow;
                }
                mergedRow = rowMerger.BuildMergedRow();
            }

            unversionedMergedRows.push_back({resultCommand, mergedRow, lockMask, /*sequentialId*/ 0});
        }

        for (const auto& submittedRow : unversionedMergedRows) {
            WriteRow(submittedRow);
        }

        PrepareVersionedRows();
    }

    void PrepareOrderedBatches()
    {
        for (const auto& submittedRow : UnversionedSubmittedRows_) {
            WriteRow(submittedRow);
        }

        PrepareVersionedRows();
    }

    void PrepareVersionedRows()
    {
        auto sorted = TableSchema_->IsSorted();
        for (const auto& typeErasedRow : VersionedSubmittedRows_) {
            IncrementAndCheckRowCount();

            auto* batch = GetBatch();
            ++batch->RowCount;

            auto* writer = batch->Writer.get();
            writer->WriteCommand(EWireProtocolCommand::VersionedWriteRow);

            if (sorted) {
                TVersionedRow row(typeErasedRow);
                batch->DataWeight += GetDataWeight(row);
                writer->WriteVersionedRow(row);
            } else {
                TUnversionedRow row(typeErasedRow);
                batch->DataWeight += GetDataWeight(row);
                writer->WriteUnversionedRow(row);
            }
        }
    }

    void WriteRow(const TUnversionedSubmittedRow& submittedRow)
    {
        IncrementAndCheckRowCount();

        auto* batch = GetBatch();
        auto* writer = batch->Writer.get();
        ++batch->RowCount;
        batch->DataWeight += GetDataWeight(submittedRow.Row);

        writer->WriteCommand(submittedRow.Command);
        writer->WriteUnversionedRow(submittedRow.Row);

        if (submittedRow.Command == EWireProtocolCommand::WriteAndLockRow) {
            writer->WriteLockMask(submittedRow.Locks);
        }
    }

    bool IsNewBatchNeeded() const
    {
        if (Batches_.empty()) {
            return true;
        }

        const auto& lastBatch = Batches_.back();
        if (Options_.MaxRowsPerBatch &&
            lastBatch->RowCount >= Options_.MaxRowsPerBatch)
        {
            return true;
        }

        if (Options_.MaxDataWeightPerBatch &&
            lastBatch->DataWeight >= Options_.MaxDataWeightPerBatch)
        {
            return true;
        }

        return false;
    }

    TBatch* GetBatch()
    {
        if (IsNewBatchNeeded()) {
            Batches_.emplace_back(new TBatch());
        }
        return Batches_.back().get();
    }

    void IncrementAndCheckRowCount()
    {
        ++TotalRowCount_;
        if (Options_.MaxRowsPerTablet &&
            TotalRowCount_ > Options_.MaxRowsPerTablet)
        {
            THROW_ERROR_EXCEPTION(
                NTabletClient::EErrorCode::TooManyRowsInTransaction,
                "Transaction affects too many rows in tablet")
                << TErrorAttribute("limit", Options_.MaxRowsPerTablet);
        }
    }
};

////////////////////////////////////////////////////////////////////////////////

void ITabletRequestBatcher::TBatch::Materialize(NCompression::ECodec codecType)
{
    auto* codec = NCompression::GetCodec(codecType);
    RequestData = codec->Compress(Writer->Finish());
}

////////////////////////////////////////////////////////////////////////////////

ITabletRequestBatcherPtr CreateTabletRequestBatcher(
    TTabletRequestBatcherOptions options,
    TTableSchemaPtr tableSchema,
    TColumnEvaluatorPtr columnEvaluator)
{
    return New<TTabletRequestBatcher>(
        std::move(options),
        std::move(tableSchema),
        std::move(columnEvaluator));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NApi::NNative
