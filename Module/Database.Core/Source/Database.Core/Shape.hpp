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

#include <Zyphryon.Base/Base.hpp>

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    /// \brief Represents the columns a statement's rows have, told once and shared by every result of the statement.
    class Shape final : public Retainable<Shape>
    {
    public:

        /// \brief Constructs a shape of no columns.
        ZY_INLINE Shape()
            : mColumns { 0 }
        {
        }

        /// \brief Adds a column after the ones already added, before any result shares the shape.
        ///
        /// \param Name The name the database gives the column, which is copied.
        ZY_INLINE void AddColumn(Text Name)
        {
            mNames.Append(Name);
            mNames.Append('\0');
            ++mColumns;
        }

        /// \brief Gets how many columns each row has.
        ///
        /// \return The number of columns.
        ZY_INLINE UInt32 GetColumns() const
        {
            return mColumns;
        }

        /// \brief Gets the name of a column.
        ///
        /// \param Column The column, counted from 0, which must be one the rows have.
        /// \return The name the database gives the column.
        ZY_INLINE Text GetName(UInt32 Column) const
        {
            Text Name = mNames;

            for (; Column > 0; --Column)
            {
                Name = Name.Slice(static_cast<UInt>(StrFind(Name, '\0')) + 1);
            }
            return Name.Slice(0, static_cast<UInt>(StrFind(Name, '\0')));
        }

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Str    mNames;
        UInt32 mColumns;
    };
}