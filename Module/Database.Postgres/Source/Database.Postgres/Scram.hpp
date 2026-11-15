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
    /// \brief Represents one side's part in a SCRAM-SHA-256 exchange, which proves a password without sending it.
    class Scram final
    {
    public:

        /// \brief Begins the exchange under a nonce of its own.
        ///
        /// \return The first message to send the server.
        Str Begin();

        /// \brief Answers the server's challenge with the proof of the password.
        ///
        /// \param Challenge The server's first message, which carries its nonce, the salt and the rounds.
        /// \param Password  The password to prove.
        /// \param Output    Receives the final message to send the server.
        /// \return `true` when the challenge was well formed and carried this exchange's nonce, `false` otherwise.
        Bool Continue(Text Challenge, Text Password, Ref<Str> Output);

        /// \brief Checks that the server knew the password too.
        ///
        /// \param Verdict The server's final message, which carries its signature.
        /// \return `true` when the signature is the one the password gives, `false` otherwise.
        Bool Verify(Text Verdict) const;

    private:

        /// \brief Finds the value of a field in a message of fields split by commas.
        ///
        /// \param Message The message, as `r=...,s=...,i=...`.
        /// \param Name    The letter the field goes by.
        /// \return The value of the field, or empty when the message lacks it.
        static Text Field(Text Message, Char Name);

        /// \brief Writes bytes out as base64.
        ///
        /// \param Data   The bytes to write.
        /// \param Output Receives the text, added to its end.
        static void Encode(ConstSpan<Byte> Data, Ref<Str> Output);

        /// \brief Reads base64 back into bytes.
        ///
        /// \param Data   The text to read.
        /// \param Output Receives the bytes.
        /// \return `true` when the text was base64, `false` otherwise.
        static Bool Decode(Text Data, Ref<Sequence<Byte>> Output);

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Str         mNonce;
        SHA256::Sum mSignature;
    };
}