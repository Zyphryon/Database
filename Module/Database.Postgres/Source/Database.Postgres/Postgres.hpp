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

#include "Scram.hpp"
#include <Database.Core/Connection.hpp>
#include <Zyphryon.Base/Thread/Thread.hpp>
#include <Zyphryon.Network/Proactor.hpp>
#include <Zyphryon.Platform/Timer.hpp>

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    /// \brief Represents a link to a PostgreSQL server, spoken to in its own protocol over the engine's sockets.
    class Postgres final : public Connection
    {
    public:

        /// \brief Reaches for the database an address names, on a thread of its own.
        ///
        /// \param Address The address, as `user:password@host:port/name`, then `?tls=on` or `?tls=verified&ca=file`.
        explicit Postgres(Text Address);

        /// \brief Waits for the queries still out to be answered, then closes the link.
        ~Postgres() override;

    protected:

        /// \see Connection::Send
        void Send(AnyRef<Sequence<Query>> Requests, AnyRef<Answer> Callback) override;

    private:

        /// \brief Maximum number of queries and batches sent ahead of their answers, topped up once half are answered.
        static constexpr UInt32 kWindow = 512;

        /// \brief Maximum time, in milliseconds, the thread waits on the platform before it looks for new queries.
        static constexpr UInt32 kIdle   = 100;

        /// \brief Shortest time, in seconds, a lost link waits before it reaches for the server again.
        static constexpr Real64 kPause  = 1.0;

        /// \brief Longest time, in seconds, a lost link waits, which every failed attempt doubles the wait toward.
        static constexpr Real64 kLimit  = 30.0;

        /// \brief Number of bytes read off the socket at a time.
        static constexpr UInt32 kChunk  = 65536;

        /// \brief Maximum number of statements kept compiled, past which the one used longest ago is let go.
        static constexpr UInt32 kKeep   = 512;

        /// \brief Maximum number of texts without values remembered as sent once, past which all are forgotten.
        static constexpr UInt32 kSeen   = 4096;

        /// \brief Specifies how far the link has come.
        enum class State : UInt8
        {
            Reaching, ///< The socket is on its way to the server.
            Securing, ///< The link is being encrypted, before anything is said over it.
            Greeting, ///< The server is being told who asks and shown the password is known.
            Ready,    ///< Queries are sent and answered.
            Broken,   ///< The link is lost, and every query fails with the reason until the server is reached again.
        };

        /// \brief Represents a batch waiting to be sent.
        struct Order final
        {
            /// The queries to run as one, a lone query being a batch of one.
            Sequence<Query> Requests;

            /// The function to deliver the results with.
            Answer          Callback;
        };

        /// \brief Represents what the server told of the rows of a statement it compiled.
        struct Plan final
        {
            /// `true` once the server described the statement's rows, `false` while it still has to be asked.
            Bool             Described = false;

            /// The columns of the statement's rows, or null for a statement that reads none.
            Retainer<Shape>  Columns;

            /// The type of each column, as the server numbers it.
            Sequence<UInt32> Types;

            /// The number of the last batch that ran the statement, so the one used longest ago goes first.
            UInt64           Used      = 0;
        };

        /// \brief Represents a batch sent and not yet answered.
        struct Flight final
        {
            /// The name of each query's compiled statement, or none for a lone text the server runs as it is.
            Sequence<UInt32> Names;

            /// The function to deliver the results with.
            Answer           Callback;

            /// The results as the answers build them, one per query of the batch.
            Sequence<Result> Outcomes;

            /// The index of the result the server's answers are filling now.
            UInt32           Cursor = 0;

            /// The name of the first statement the batch compiled that the server has not confirmed.
            UInt32           First  = 0;

            /// The number of statements the batch compiled that the server has not confirmed.
            UInt32           Fresh  = 0;
        };

    private:

        /// \brief Runs the link from reaching the server to closing it.
        ///
        /// \param Token The token that tells the thread the link is closing.
        void Run(ConstRef<Thread::Token> Token);

        /// \brief Reads the address into who asks, where the server is and which database is asked for.
        void Parse();

        /// \brief Resolves the server's address and posts the attempt to reach it.
        ///
        /// \return `true` when the attempt was posted, `false` when the address or the socket failed.
        Bool Reach();

        /// \brief Tells the server which user asks for which database.
        void Greet();

        /// \brief Answers what the server asks for to believe the user.
        ///
        /// \param Body The body of the server's authentication message.
        void Authenticate(ConstSpan<Byte> Body);

        /// \brief Sends as many waiting batches as the window leaves room for.
        void Dispatch();

        /// \brief Writes a batch out for the server, closed by one sync, and records it as sent.
        ///
        /// \param Entry The batch to send, whose callback is taken.
        void Encode(Ref<Order> Entry);

        /// \brief Lets go of the statements used longest ago, here and on the server, until one more fits the bound.
        void Forget();

        /// \brief Checks whether a lone text without values runs in the simple protocol, compiled and let go.
        ///
        /// \param Statement The text, simple when a semicolon stands before its end or the first time it is sent.
        /// \return `true` to run it in the simple protocol, `false` to compile and keep it.
        Bool IsSimple(Text Statement);

        /// \brief Posts the write of whatever was written out, if none is in the air.
        void Flush();

        /// \brief Posts the next read, if none is in the air.
        void Listen();

        /// \brief Takes what a read brought off the socket, opening it first when the link is encrypted.
        ///
        /// \param Data The bytes the read brought.
        void Take(ConstSpan<Byte> Data);

        /// \brief Acts on every whole message among the plain bytes that arrived.
        ///
        /// \param Data The plain bytes that follow those still waiting for the rest of their message.
        void Absorb(ConstSpan<Byte> Data);

        /// \brief Acts on one message of the server.
        ///
        /// \param Kind The letter the message opens with.
        /// \param Body The bytes after its length.
        void Handle(Char Kind, ConstSpan<Byte> Body);

        /// \brief Finds what the server told of the statement whose answers are arriving.
        ///
        /// \param Current The batch being answered.
        /// \return The plan of the statement, or `nullptr` when the link keeps none for the query.
        Ptr<Plan> Recall(ConstRef<Flight> Current);

        /// \brief Gives the link up, failing every batch sent or waiting with the reason.
        ///
        /// \param Cause The reason, which every later query fails with too.
        void Break(Text Cause);

        /// \brief Reaches for the server again, once the wait is over and the lost link has nothing left out.
        void Mend();

        /// \brief Opens a message in the outbound queue.
        ///
        /// \param Kind The letter the message opens with, or 0 for the startup message, which has none.
        void Open(Char Kind);

        /// \brief Closes the message last opened, writing its length where it belongs.
        void Close();

        /// \brief Writes a 16-bit number into the open message, most significant byte first.
        ///
        /// \param Number The number to write.
        void Put16(UInt16 Number);

        /// \brief Writes a 32-bit number into the open message, most significant byte first.
        ///
        /// \param Number The number to write.
        void Put32(UInt32 Number);

        /// \brief Writes text into the open message, closed by a zero when it is a string of the protocol.
        ///
        /// \param Content The text to write.
        /// \param Closed  `true` to write the closing zero, `false` to write the bytes alone.
        void Put(Text Content, Bool Closed);

        /// \brief Writes a value into the open message, as its length and then the text the server reads its type from.
        ///
        /// \param Argument The value to write, a length of -1 and no text standing for one that holds nothing.
        void Put(ConstRef<Value> Argument);

        /// \brief Writes the name the server keeps a compiled statement under into the open message.
        ///
        /// \param Name The number the statement was given when it was first sent, written as `s` and the number.
        void Tag(UInt32 Name);

        /// \brief Writes a 32-bit number over four bytes of the outbound queue, most significant byte first.
        ///
        /// \param Offset The index of the first of the four bytes, which the queue must already hold.
        /// \param Number The number to write.
        ZY_INLINE void Poke(UInt Offset, UInt32 Number)
        {
            mQueue[Offset + 0] = static_cast<Byte>(Number >> 24);
            mQueue[Offset + 1] = static_cast<Byte>(Number >> 16);
            mQueue[Offset + 2] = static_cast<Byte>(Number >> 8);
            mQueue[Offset + 3] = static_cast<Byte>(Number);
        }

        /// \brief Reads a 32-bit number the server wrote, most significant byte first.
        ///
        /// \param Data The four bytes of the number.
        /// \return The number.
        ZY_INLINE static constexpr UInt32 Peek(ConstPtr<Byte> Data)
        {
            return (static_cast<UInt32>(Data[0]) << 24) | (static_cast<UInt32>(Data[1]) << 16)
                 | (static_cast<UInt32>(Data[2]) << 8)  |  static_cast<UInt32>(Data[3]);
        }

        /// \brief Finds why the server refused something among the fields of its error message.
        ///
        /// \param Body     The bytes of the error message after its length.
        /// \param Fallback The reason to give when the message carries none.
        /// \return The reason in the server's words, which lives as long as the message, or the fallback.
        static Text Reason(ConstSpan<Byte> Body, Text Fallback);

        /// \brief Parts a text in two at the first occurrence of a character.
        ///
        /// \param Whole The text to part.
        /// \param Mark  The character to part at, which goes to neither side.
        /// \param Tail  Receives the text after the character, or nothing when the text does not hold it.
        /// \return The text before the character, or all of it when the text does not hold it.
        static Text Part(Text Whole, Char Mark, Ref<Text> Tail);

        /// \brief Reads a cell the server sent as text into the value its type stands for.
        ///
        /// \param Type The type of the column, as the server numbers it.
        /// \param Cell The text of the cell.
        /// \return The value, a whole number, a real, bytes or text by the type.
        static Value Read(UInt32 Type, Text Cell);

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Str                 mAddress;
        Str                 mUser;
        Str                 mPassword;
        Str                 mHost;
        Str                 mName;
        UInt16              mPort;
        State               mState;
        Str                 mCause;
        ZyPlatform::Timer   mClock;
        Real64              mPause;
        Real64              mRetry;
        Scram               mScram;
        ZyNetwork::Proactor mProactor;
        ZyNetwork::Socket   mSocket;
        Bool                mSending;
        Bool                mReceiving;
        Bool                mSecure;
        Bool                mVerified;
        Bool                mAsked;
        TLS                 mTunnel;
        Sequence<Byte>      mAuthority;
        Sequence<Byte>      mChunk;
        Sequence<Byte>      mInbound;
        Sequence<Byte>      mPlain;
        Sequence<Byte>      mRaw;
        Sequence<Byte>      mWire;
        Sequence<Byte>      mQueue;
        UInt                mOpened;
        Sequence<Order>     mWaiting;
        UInt                mNext;
        Sequence<Flight>    mFlights;
        UInt                mHead;
        Sequence<UInt32>    mTypes;
        Table<Str, UInt32>  mPrepared;
        Table<UInt32, Plan> mPlans;
        UInt32              mNames;
        UInt64              mSent;
        Bag<UInt64>         mSeen;
        Mutex               mMutex;
        Sequence<Order>     mOrders;
        Atomic<Bool>        mAwake;
        Atomic<Bool>        mRoused;
        Thread              mThread;
    };
}