// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// Copyright (C) 2021-2026 by Agustin L. Alvarez. All rights reserved.
//
// This work is licensed under the terms of the MIT license.
//
// For a copy, see <https://opensource.org/licenses/MIT>.
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

#pragma once

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [  HEADER  ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

#include "Row.hpp"
#include "Shape.hpp"

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    /// \brief Represents what a query came back with: the rows it read, how many it changed, or why it failed.
    class Result final
    {
    public:

        /// \brief Constructs a result that read nothing, changed nothing and did not fail.
        ZY_INLINE Result()
            : mAffected { 0 }
        {
        }

        /// \brief Marks the result as failed, dropping whatever it had read.
        ///
        /// \param Reason The reason the database gave, which is copied.
        ZY_INLINE void Fail(Text Reason)
        {
            mError = Reason.IsEmpty() ? "The query failed" : Reason;
            mShape = nullptr;
            mCells.Clear();
        }

        /// \brief Checks whether the query ran.
        ///
        /// \return `true` when the query ran, `false` when the database refused it.
        ZY_INLINE Bool IsOk() const
        {
            return mError.IsEmpty();
        }

        /// \brief Gets why the query failed.
        ///
        /// \return The reason the database gave, or empty when the query ran.
        ZY_INLINE Text GetError() const
        {
            return mError;
        }

        /// \brief Sets how many rows the query changed.
        ///
        /// \param Affected The number of rows inserted, updated or deleted.
        ZY_INLINE void SetAffected(UInt64 Affected)
        {
            mAffected = Affected;
        }

        /// \brief Gets how many rows the query changed.
        ///
        /// \return The number of rows inserted, updated or deleted, 0 for a query that only reads.
        ZY_INLINE UInt64 GetAffected() const
        {
            return mAffected;
        }

        /// \brief Sets the columns the rows have, before any cell is added.
        ///
        /// \param Columns The columns to give the rows, which every result of the statement shares.
        ZY_INLINE void SetShape(ConstRetainer<Shape> Columns)
        {
            mShape = Columns;
        }

        /// \brief Adds the next cell, rows filling left to right and then top to bottom.
        ///
        /// \param Cell The cell to add.
        ZY_INLINE void AddCell(AnyRef<Value> Cell)
        {
            mCells.Append(Move(Cell));
        }

        /// \brief Gets how many columns each row has.
        ///
        /// \return The number of columns, 0 for a query that reads nothing.
        ZY_INLINE UInt32 GetColumns() const
        {
            return mShape ? mShape->GetColumns() : 0;
        }

        /// \brief Gets how many rows the query read.
        ///
        /// \return The number of whole rows.
        ZY_INLINE UInt32 GetRows() const
        {
            const UInt32 Columns = GetColumns();

            return Columns == 0 ? 0 : static_cast<UInt32>(mCells.GetSize() / Columns);
        }

        /// \brief Gets the name of a column.
        ///
        /// \param Column The column, counted from 0, which must be one the rows have.
        /// \return The name the database gives the column.
        ZY_INLINE Text GetName(UInt32 Column) const
        {
            return mShape->GetName(Column);
        }

        /// \brief Gets a row to read a cell at a time.
        ///
        /// \param Index The row, counted from 0, which must be one the query read.
        /// \return The row, whose cells live as long as the result.
        ZY_INLINE Row GetRow(UInt32 Index) const
        {
            return Row(ConstSpan(mCells.GetData() + Index * GetColumns(), GetColumns()));
        }

        /// \brief Gets a cell.
        ///
        /// \param Row    The row, counted from 0, which must be one the query read.
        /// \param Column The column, counted from 0, which must be one the rows have.
        /// \return The cell, which lives as long as the result.
        ZY_INLINE ConstRef<Value> GetValue(UInt32 Row, UInt32 Column) const
        {
            return mCells[Row * GetColumns() + Column];
        }

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Str             mError;
        UInt64          mAffected;
        Retainer<Shape> mShape;
        Sequence<Value> mCells;
    };
}