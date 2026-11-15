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

#include "Query.hpp"
#include "Result.hpp"
#include <Zyphryon.Base/Thread/Atomic.hpp>
#include <Zyphryon.Base/Thread/Mutex.hpp>

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    /// \brief Represents a link to one database, which runs queries in the order sent and never blocks who sends them.
    class Connection
    {
    public:

        /// \brief The function a query's result is handed to, on the thread that polls the connection.
        using Reply  = Delegate<void(ConstRef<Result>)>;

        /// \brief The function a batch's results are handed to, one per query in order, all failed if one was refused.
        using Answer = Delegate<void(ConstSpan<Result>)>;

    public:

        /// \brief Constructs a connection waiting on nothing.
        Connection();

        /// \brief Closes the connection, dropping the results nobody polled.
        virtual ~Connection() = default;

        /// \brief Sends a query, which runs after every query sent before it.
        ///
        /// \param Request  The query to run.
        /// \param Callback The function handed the result on a later \ref Poll, or nothing to drop it.
        void Submit(AnyRef<Query> Request, AnyRef<Reply> Callback);

        /// \brief Sends queries as one batch, which the database runs whole or not at all, after what was sent before.
        ///
        /// \param Requests The queries to run in order, at least one, each holding a single statement.
        /// \param Callback The function handed the results on a later \ref Poll, or nothing to drop them.
        void Submit(AnyRef<Sequence<Query>> Requests, AnyRef<Answer> Callback);

        /// \brief Hands every result that arrived since the last call to the function its query was sent with.
        ///
        /// \return The number of queries and batches still unanswered.
        UInt32 Poll();

        /// \brief Gets how many queries and batches were sent and not yet answered.
        ///
        /// \return The number of queries and batches whose results were not handed out.
        ZY_INLINE UInt32 GetPending() const
        {
            return mPending;
        }

    protected:

        /// \brief Runs a batch on the database, off the calling thread, and delivers its results when they are known.
        ///
        /// \param Requests The queries to run in order, whole or not at all, a lone query being a batch of one.
        /// \param Callback The function to deliver the results with.
        virtual void Send(AnyRef<Sequence<Query>> Requests, AnyRef<Answer> Callback) = 0;

        /// \brief Keeps a batch's results until the next poll hands them out, from whichever thread learnt them.
        ///
        /// \param Callback The function the batch was sent with.
        /// \param Outcomes The results, one per query of the batch.
        void Deliver(AnyRef<Answer> Callback, AnyRef<Sequence<Result>> Outcomes);

        /// \brief Makes the results of a batch that did not run, every one failed for the same reason.
        ///
        /// \param Count  The number of queries the batch holds.
        /// \param Reason The reason the batch did not run, which is copied into every result.
        /// \return The results, one per query of the batch.
        static Sequence<Result> Refuse(UInt Count, Text Reason);

    private:

        /// \brief Represents a batch's results waiting for the poll that hands them out.
        struct Parcel final
        {
            /// The function the batch was sent with.
            Answer           Callback;

            /// The results, one per query of the batch.
            Sequence<Result> Outcomes;
        };

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Mutex            mMutex;
        Sequence<Parcel> mArrived;
        Atomic<UInt32>   mPending;
    };
}