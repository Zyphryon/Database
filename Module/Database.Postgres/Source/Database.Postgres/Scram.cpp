// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// Copyright (C) 2021-2026 by Agustin L. Alvarez. All rights reserved.
//
// This work is licensed under the terms of the MIT license.
//
// For a copy, see <https://opensource.org/licenses/MIT>.
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [  HEADER  ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

#include "Scram.hpp"

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Str Scram::Begin()
    {
        Array<Byte, 18> Random { };
        Entropy::Gather(Random);

        mNonce.Clear();
        Encode(Random, mNonce);

        // The server takes the name from the startup message, so the one here stays empty.
        Str Output("n,,n=,r=");
        Output.Append(mNonce);
        return Output;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Bool Scram::Continue(Text Challenge, Text Password, Ref<Str> Output)
    {
        const Text     Nonce  = Field(Challenge, 'r');
        Sequence<Byte> Salt;
        UInt           Cursor = 0;
        const UInt32   Rounds = StrExtractNumber<10, UInt32>(Field(Challenge, 'i'), Cursor);

        // The server's nonce has to open with this exchange's own, or the challenge answers somebody else.
        if (Nonce.GetSize() <= mNonce.GetSize() || !StrStartsWith(Nonce, mNonce))
        {
            return false;
        }

        if (Rounds == 0 || !Decode(Field(Challenge, 's'), Salt))
        {
            return false;
        }

        Str Bare("c=biws,r=");
        Bare.Append(Nonce);

        Str Message("n=,r=");
        Message.Append(mNonce);
        Message.Append(',');
        Message.Append(Challenge);
        Message.Append(',');
        Message.Append(Bare);

        const ConstSpan<Byte> Spoken = Text(Message).GetBytes();

        const SHA256::Sum Salted = SHA256::Derive(Password.GetBytes(), Salt, Rounds);
        const ConstSpan<Byte> Root = Salted;

        const SHA256::Sum Client = SHA256::Sign(Root, Text("Client Key").GetBytes());
        const SHA256::Sum Stored = SHA256::Hash(Client);
        const SHA256::Sum Signed = SHA256::Sign(Stored, Spoken);
        const SHA256::Sum Server = SHA256::Sign(Root, Text("Server Key").GetBytes());

        mSignature = SHA256::Sign(Server, Spoken);

        SHA256::Sum Proof;

        for (UInt32 Index = 0; Index < SHA256::kSize; ++Index)
        {
            Proof[Index] = Client[Index] ^ Signed[Index];
        }

        Output = Bare;
        Output.Append(",p=");
        Encode(Proof, Output);
        return true;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Bool Scram::Verify(Text Verdict) const
    {
        Sequence<Byte> Given;

        if (!Decode(Field(Verdict, 'v'), Given) || Given.GetSize() != SHA256::kSize)
        {
            return false;
        }

        // Every byte is weighed, so how long the check takes tells nothing of where the signatures part.
        Byte Difference = 0;

        for (UInt32 Index = 0; Index < SHA256::kSize; ++Index)
        {
            Difference |= static_cast<Byte>(Given[Index] ^ mSignature[Index]);
        }
        return Difference == 0;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Text Scram::Field(Text Message, Char Name)
    {
        for (UInt Start = 0; Start < Message.GetSize();)
        {
            UInt End = Start;

            while (End < Message.GetSize() && Message[End] != ',')
            {
                ++End;
            }
            if (End - Start >= 2 && Message[Start] == Name && Message[Start + 1] == '=')
            {
                return Message.Slice(Start + 2, End - Start - 2);
            }
            Start = End + 1;
        }
        return Text();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Scram::Encode(ConstSpan<Byte> Data, Ref<Str> Output)
    {
        static constexpr Text kLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        for (UInt Index = 0; Index < Data.GetSize(); Index += 3)
        {
            const UInt   Left  = Data.GetSize() - Index;
            const UInt32 Three = (static_cast<UInt32>(Data[Index]) << 16)
                               | (Left > 1 ? static_cast<UInt32>(Data[Index + 1]) << 8 : 0)
                               | (Left > 2 ? static_cast<UInt32>(Data[Index + 2]) : 0);

            Output.Append(kLetters[(Three >> 18) & 63]);
            Output.Append(kLetters[(Three >> 12) & 63]);
            Output.Append(Left > 1 ? kLetters[(Three >> 6) & 63] : '=');
            Output.Append(Left > 2 ? kLetters[Three & 63] : '=');
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Bool Scram::Decode(Text Data, Ref<Sequence<Byte>> Output)
    {
        UInt32 Held = 0;
        UInt32 Bits = 0;

        if (Data.IsEmpty() || Data.GetSize() % 4 != 0)
        {
            return false;
        }

        for (UInt Index = 0; Index < Data.GetSize(); ++Index)
        {
            const Char Letter = Data[Index];
            UInt32     Six    = 0;

            if (Letter >= 'A' && Letter <= 'Z')
            {
                Six = static_cast<UInt32>(Letter - 'A');
            }
            else if (Letter >= 'a' && Letter <= 'z')
            {
                Six = static_cast<UInt32>(Letter - 'a') + 26;
            }
            else if (Letter >= '0' && Letter <= '9')
            {
                Six = static_cast<UInt32>(Letter - '0') + 52;
            }
            else if (Letter == '+' || Letter == '/')
            {
                Six = (Letter == '+' ? 62 : 63);
            }
            else if (Letter == '=' && Index + 2 >= Data.GetSize())
            {
                break;
            }
            else
            {
                return false;
            }

            Held  = (Held << 6) | Six;
            Bits += 6;

            if (Bits >= 8)
            {
                Bits -= 8;
                Output.Append(static_cast<Byte>((Held >> Bits) & 0xFF));
            }
        }
        return true;
    }
}