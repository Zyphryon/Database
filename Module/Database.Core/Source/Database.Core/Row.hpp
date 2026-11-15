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

#include "Value.hpp"

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    /// \brief Represents one row of a result, read a cell at a time from its first column to its last.
    class Row final
    {
    public:

        /// \brief Constructs a row over its cells, about to read the first.
        ///
        /// \param Cells The cells of the row in column order, which must outlive the row.
        ZY_INLINE explicit Row(ConstSpan<Value> Cells)
            : mCells { Cells },
              mNext  { 0 }
        {
        }

        /// \brief Reads the next cell as the type asked for.
        ///
        /// \tparam Type The type to read, a whole number, a real, `Bool`, `Text`, `Str` or `ConstSpan<Byte>`.
        /// \return The cell as that type, zero or empty when the cell holds nothing or another kind of value.
        template<typename Type>
        ZY_INLINE Type Read()
        {
            ZY_ASSERT(mNext < mCells.GetSize(), "The row has no cell left to read");

            ConstRef<Value> Cell = mCells[mNext++];

            if constexpr (IsAnyOf<Type, Bool>)
            {
                return Cell.GetInteger() != 0;
            }
            else if constexpr (IsIntegral<Type>)
            {
                return static_cast<Type>(Cell.GetInteger());
            }
            else if constexpr (IsReal<Type>)
            {
                return static_cast<Type>(Cell.GetReal());
            }
            else if constexpr (IsAnyOf<Type, ConstSpan<Byte>>)
            {
                return Cell.GetBytes();
            }
            else
            {
                return Type(Cell.GetText());
            }
        }

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        ConstSpan<Value> mCells;
        UInt             mNext;
    };
}