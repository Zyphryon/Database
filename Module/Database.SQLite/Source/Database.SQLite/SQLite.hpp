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

#include <Database.Core/Connection.hpp>
#include <Zyphryon.Base/Thread/Gate.hpp>
#include <Zyphryon.Base/Thread/Thread.hpp>

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

struct sqlite3;
struct sqlite3_stmt;

namespace ZyPlugin::Database
{
    /// \brief Represents a link to a database kept in one file, whose queries run on a thread of its own.
    class SQLite final : public Connection
    {
    public:

        /// \brief Opens the database a file holds, creating the file when it is missing.
        ///
        /// \param Path The file to open, or `:memory:` for a database that lives only as long as the link.
        explicit SQLite(Text Path);

        /// \brief Runs the queries still waiting, then closes the database.
        ~SQLite() override;

        /// \brief Checks whether the database was opened.
        ///
        /// \return `true` when queries can run, `false` when the file could not be opened and every query fails.
        ZY_INLINE Bool IsOpen() const
        {
            return mHandle != nullptr;
        }

    protected:

        /// \see Connection::Send
        void Send(AnyRef<Sequence<Query>> Requests, AnyRef<Answer> Callback) override;

    private:

        /// \brief Maximum number of statements kept compiled, past which the one used longest ago is let go.
        static constexpr UInt32 kKeep = 512;

        /// \brief Maximum number of texts without values remembered as sent once, past which all are forgotten.
        static constexpr UInt32 kSeen = 4096;

        /// \brief Represents a batch waiting for the thread to run it.
        struct Order final
        {
            /// The queries to run as one, a lone query being a batch of one.
            Sequence<Query> Requests;

            /// The function to deliver the results with.
            Answer          Callback;
        };

        /// \brief Represents a statement the database compiled, kept to run again.
        struct Plan final
        {
            /// The statement as the database compiled it.
            Ptr<sqlite3_stmt> Compiled = nullptr;

            /// The columns of the statement's rows, or null until it first runs and when it reads none.
            Retainer<Shape>   Columns;

            /// The count of queries run when the statement last ran, so the one used longest ago goes first.
            UInt64            Used     = 0;
        };

    private:

        /// \brief Opens the database a file holds and sets it up for one writer beside many readers.
        ///
        /// \param Path The file to open.
        /// \return The database, or `nullptr` when the file could not be opened.
        static Ptr<sqlite3> Open(Text Path);

        /// \brief Runs the batches as they arrive, until the link is closed and none is left.
        ///
        /// \param Token The token that tells the thread the link is closing.
        void Run(ConstRef<Thread::Token> Token);

        /// \brief Runs a batch inside a savepoint, which is rolled back when one of its queries is refused.
        ///
        /// \param Requests The queries to run in order.
        /// \return The results, one per query, every one failed when the batch was rolled back.
        Sequence<Result> Perform(ConstSpan<Query> Requests);

        /// \brief Runs one query and collects what it came back with.
        ///
        /// \param Request The query to run.
        /// \param Outcome Receives the rows read, the rows changed or the reason it failed.
        void Execute(ConstRef<Query> Request, Ref<Result> Outcome);

        /// \brief Finds the compiled statement of a query, compiling and keeping it the first time it is worth keeping.
        ///
        /// \param Request The query, whose text is kept from its first use with values and from its second without.
        /// \param Outcome Receives the reason the statement could not be compiled.
        /// \return The statement as kept, or `nullptr` when refused, not yet worth keeping, or of several statements.
        Ptr<Plan> Prepare(ConstRef<Query> Request, Ref<Result> Outcome);

        /// \brief Lets go of the statement used longest ago, to make room for one more.
        void Forget();

        /// \brief Binds the values of a query to the placeholders of its statement.
        ///
        /// \param Statement The compiled statement.
        /// \param Arguments The values, the first for `$1`.
        /// \param Outcome   Receives the reason a placeholder could not be bound.
        /// \return `true` when every placeholder took its value, `false` otherwise.
        Bool Bind(Ptr<sqlite3_stmt> Statement, ConstSpan<Value> Arguments, Ref<Result> Outcome) const;

        /// \brief Steps a statement to its end, collecting its rows.
        ///
        /// \param Known   The statement to run, its placeholders bound, its columns told when null or changed in count.
        /// \param Outcome Receives the rows read, the rows changed or the reason it failed.
        void Step(Ref<Plan> Known, Ref<Result> Outcome) const;

    private:

        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
        // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

        Ptr<sqlite3>     mHandle;
        Mutex            mMutex;
        Gate             mGate;
        Sequence<Order>  mOrders;
        Table<Str, Plan> mPrepared;
        UInt64           mQueries;
        Bag<UInt64>      mSeen;
        Thread           mThread;
    };
}