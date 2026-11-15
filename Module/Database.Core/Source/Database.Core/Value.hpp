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
    /// \brief Represents one cell a query sends or a row brings back, which is nothing, a number, text or bytes.
    class Value final
    {
    public:

        /// \brief The bytes a value holds.
        using Bytes = Sequence<Byte>;

    public:

        /// \brief Constructs a value holding nothing.
        ZY_INLINE Value() = default;

        /// \brief Constructs a value holding a whole number.
        ///
        /// \param Number The number to hold, widened to 64 bits.
        template<IsIntegral Type>
        ZY_INLINE Value(Type Number)
            : mData { static_cast<SInt64>(Number) }
        {
        }

        /// \brief Constructs a value holding a real.
        ///
        /// \param Number The number to hold, widened to 64 bits.
        template<IsReal Type>
        ZY_INLINE Value(Type Number)
            : mData { static_cast<Real64>(Number) }
        {
        }

        /// \brief Constructs a value holding text.
        ///
        /// \param Content The text to hold, which is copied.
        ZY_INLINE Value(Text Content)
            : mData { Str(Content) }
        {
        }

        /// \brief Constructs a value holding bytes.
        ///
        /// \param Content The bytes to hold, which are copied.
        ZY_INLINE Value(ConstSpan<Byte> Content)
        {
            mData.Emplace<Bytes>().Append(Content);
        }

        /// \brief Checks whether the value holds nothing.
        ///
        /// \return `true` when the value is null, `false` when it holds a number, text or bytes.
        ZY_INLINE Bool IsNull() const
        {
            return mData.IsEmpty();
        }

        /// \brief Checks whether the value holds a whole number.
        ///
        /// \return `true` when the value is a whole number, `false` otherwise.
        ZY_INLINE Bool IsInteger() const
        {
            return mData.IsHolding<SInt64>();
        }

        /// \brief Checks whether the value holds a real.
        ///
        /// \return `true` when the value is a real, `false` otherwise.
        ZY_INLINE Bool IsReal() const
        {
            return mData.IsHolding<Real64>();
        }

        /// \brief Checks whether the value holds text.
        ///
        /// \return `true` when the value is text, `false` otherwise.
        ZY_INLINE Bool IsText() const
        {
            return mData.IsHolding<Str>();
        }

        /// \brief Checks whether the value holds bytes.
        ///
        /// \return `true` when the value is bytes, `false` otherwise.
        ZY_INLINE Bool IsBytes() const
        {
            return mData.IsHolding<Bytes>();
        }

        /// \brief Gets the value as a whole number.
        ///
        /// \return The number, a real cut to its whole part, or 0 for anything else.
        ZY_INLINE SInt64 GetInteger() const
        {
            if (const ConstPtr<SInt64> Whole = mData.TryGet<SInt64>())
            {
                return * Whole;
            }
            if (const ConstPtr<Real64> Real = mData.TryGet<Real64>())
            {
                return static_cast<SInt64>(* Real);
            }
            return 0;
        }

        /// \brief Gets the value as a real.
        ///
        /// \return The number, a whole one widened, or 0 for anything else.
        ZY_INLINE Real64 GetReal() const
        {
            if (const ConstPtr<Real64> Real = mData.TryGet<Real64>())
            {
                return * Real;
            }
            if (const ConstPtr<SInt64> Whole = mData.TryGet<SInt64>())
            {
                return static_cast<Real64>(* Whole);
            }
            return 0.0;
        }

        /// \brief Gets the value as text.
        ///
        /// \return The text, which lives as long as the value, or empty for anything else.
        ZY_INLINE Text GetText() const
        {
            const ConstPtr<Str> Content = mData.TryGet<Str>();
            return Content ? Text(* Content) : Text();
        }

        /// \brief Gets the value as bytes.
        ///
        /// \return The bytes, which live as long as the value, or none for anything else.
        ZY_INLINE ConstSpan<Byte> GetBytes() const
        {
            const ConstPtr<Bytes> Content = mData.TryGet<Bytes>();
            return Content ? ConstSpan<Byte>(* Content) : ConstSpan<Byte>();
        }

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Variant<SInt64, Real64, Str, Bytes> mData;
    };
}