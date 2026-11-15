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

#include "SQLite.hpp"
#include <sqlite3.h>

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    SQLite::SQLite(Text Path)
        : mHandle   { Open(Path) },
          mQueries  { 0 },
          mThread   { Thread::Work::Create<& SQLite::Run>(this) }
    {
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Ptr<sqlite3> SQLite::Open(Text Path)
    {
        const Str    Where(Path);
        Ptr<sqlite3> Handle = nullptr;

        if (sqlite3_open(Where.GetData(), AddressOf(Handle)) != SQLITE_OK)
        {
            LOG_E("Database: Unable to open '{0}'", Path);

            sqlite3_close(Handle);
            return nullptr;
        }

        // Writers no longer block readers, and a commit costs one sync less.
        sqlite3_exec(Handle, "PRAGMA journal_mode = WAL", nullptr, nullptr, nullptr);
        sqlite3_exec(Handle, "PRAGMA synchronous = NORMAL", nullptr, nullptr, nullptr);
        sqlite3_exec(Handle, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);
        return Handle;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    SQLite::~SQLite()
    {
        {
            const Guard Hold(mMutex);

            mThread.Stop();
        }
        mGate.Broadcast();
        mThread.Join();

        mPrepared.ForEach([](ConstRef<Str>, ConstRef<Plan> Known)
        {
            sqlite3_finalize(Known.Compiled);
        });
        sqlite3_close(mHandle);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void SQLite::Send(AnyRef<Sequence<Query>> Requests, AnyRef<Answer> Callback)
    {
        {
            const Guard Hold(mMutex);

            mOrders.Append(Move(Requests), Move(Callback));
        }
        mGate.Signal();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void SQLite::Run(ConstRef<Thread::Token> Token)
    {
        while (true)
        {
            Sequence<Order> Taken;
            {
                Lock Barrier(mMutex);

                mGate.Wait(Barrier, [this, & Token]()
                {
                    return !mOrders.IsEmpty() || Token.Stopped();
                });

                if (mOrders.IsEmpty())
                {
                    return;
                }
                Taken = Move(mOrders);
                mOrders.Clear();
            }

            for (UInt Index = 0; Index < Taken.GetSize(); ++Index)
            {
                Deliver(Move(Taken[Index].Callback), Perform(Taken[Index].Requests));
            }
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Sequence<Result> SQLite::Perform(ConstSpan<Query> Requests)
    {
        if (mHandle == nullptr)
        {
            return Refuse(Requests.GetSize(), "The database is not open");
        }

        Sequence<Result> Outcomes(Requests.GetSize());
        Text             Reason;

        if (Requests.GetSize() == 1)
        {
            Execute(Requests[0], Outcomes.Append());
            return Outcomes;
        }

        sqlite3_exec(mHandle, "SAVEPOINT batch", nullptr, nullptr, nullptr);

        for (UInt Index = 0; Index < Requests.GetSize() && Reason.IsEmpty(); ++Index)
        {
            Execute(Requests[Index], Outcomes.Append());
            Reason = Outcomes[Index].GetError();
        }

        if (Reason.IsEmpty() && sqlite3_exec(mHandle, "RELEASE batch", nullptr, nullptr, nullptr) != SQLITE_OK)
        {
            Reason = StrConvert(sqlite3_errmsg(mHandle));
        }

        if (!Reason.IsEmpty())
        {
            Outcomes = Refuse(Requests.GetSize(), Reason);
            sqlite3_exec(mHandle, "ROLLBACK TO batch; RELEASE batch", nullptr, nullptr, nullptr);
        }
        return Outcomes;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void SQLite::Execute(ConstRef<Query> Request, Ref<Result> Outcome)
    {
        const Text Statement = Request.GetStatement();

        if (const Ptr<Plan> Known = Prepare(Request, Outcome))
        {
            if (Bind(Known->Compiled, Request.GetArguments(), Outcome))
            {
                Step(* Known, Outcome);
            }
            sqlite3_reset(Known->Compiled);
            sqlite3_clear_bindings(Known->Compiled);
            return;
        }

        // A text not kept, as a schema of several statements is, runs each statement in turn.
        ConstPtr<Char>       Cursor = Statement.GetData();
        const ConstPtr<Char> Last   = Statement.GetData() + Statement.GetSize();

        while (Cursor < Last && Outcome.IsOk())
        {
            Plan           Once;
            ConstPtr<Char> Tail = nullptr;

            if (sqlite3_prepare_v2(mHandle, Cursor, Last - Cursor, AddressOf(Once.Compiled),
                                   AddressOf(Tail)) != SQLITE_OK)
            {
                Outcome.Fail(StrConvert(sqlite3_errmsg(mHandle)));
                break;
            }
            if (Once.Compiled)
            {
                Outcome = Result();
                Step(Once, Outcome);
                sqlite3_finalize(Once.Compiled);
            }
            Cursor = Tail;
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Ptr<SQLite::Plan> SQLite::Prepare(ConstRef<Query> Request, Ref<Result> Outcome)
    {
        const Text Statement = Request.GetStatement();

        if (const Ptr<Plan> Known = mPrepared.Find(Statement))
        {
            Known->Used = ++mQueries;
            return Known;
        }

        // A text without values is kept from its second use, so one sent once is compiled and let go.
        if (Request.GetArguments().IsEmpty())
        {
            if (mSeen.GetSize() >= kSeen)
            {
                mSeen.Clear();
            }

            if (mSeen.Insert(Statement.Hash()))
            {
                return nullptr;
            }
        }

        Ptr<sqlite3_stmt> Compiled = nullptr;
        ConstPtr<Char>    Tail     = nullptr;

        const SInt32 Status = sqlite3_prepare_v3(mHandle, Statement.GetData(), Statement.GetSize(),
                                                 SQLITE_PREPARE_PERSISTENT, AddressOf(Compiled), AddressOf(Tail));

        if (Status != SQLITE_OK)
        {
            Outcome.Fail(StrConvert(sqlite3_errmsg(mHandle)));
            return nullptr;
        }

        // A text of several statements or of none is not kept, and runs as it is.
        if (!Compiled || !StrTrim(Text(Tail, Statement.GetData() + Statement.GetSize())).IsEmpty())
        {
            sqlite3_finalize(Compiled);
            return nullptr;
        }

        if (mPrepared.GetSize() >= kKeep)
        {
            Forget();
        }

        Ref<Plan> Kept = mPrepared.FindOrInsert(Str(Statement));
        Kept.Compiled  = Compiled;
        Kept.Used      = ++mQueries;
        return AddressOf(Kept);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void SQLite::Forget()
    {
        UInt64 Oldest = kMaximum<UInt64>;

        mPrepared.ForEach([& Oldest](ConstRef<Str>, ConstRef<Plan> Known)
        {
            Oldest = Min(Oldest, Known.Used);
        });

        mPrepared.EraseIf([Oldest](ConstRef<Str>, ConstRef<Plan> Known)
        {
            if (Known.Used != Oldest)
            {
                return false;
            }
            sqlite3_finalize(Known.Compiled);
            return true;
        });
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Bool SQLite::Bind(Ptr<sqlite3_stmt> Statement, ConstSpan<Value> Arguments, Ref<Result> Outcome) const
    {
        const SInt32 Count = sqlite3_bind_parameter_count(Statement);

        for (SInt32 Slot = 1; Slot <= Count; ++Slot)
        {
            // A placeholder written `$3` takes the third value wherever it stands; any other takes them in order.
            const ConstPtr<Char> Name   = sqlite3_bind_parameter_name(Statement, Slot);
            UInt                 Cursor = 1;
            UInt                 Index  = Slot;

            if (Name && Name[0] == '$')
            {
                Index = StrExtractNumber<10, UInt>(StrConvert(Name), Cursor);
            }

            if (Index < 1 || Index > Arguments.GetSize())
            {
                Outcome.Fail("The statement has a placeholder no value was given for");
                return false;
            }

            ConstRef<Value> Argument = Arguments[Index - 1];
            SInt32          Status   = SQLITE_OK;

            if (Argument.IsNull())
            {
                Status = sqlite3_bind_null(Statement, Slot);
            }
            else if (Argument.IsInteger())
            {
                Status = sqlite3_bind_int64(Statement, Slot, Argument.GetInteger());
            }
            else if (Argument.IsReal())
            {
                Status = sqlite3_bind_double(Statement, Slot, Argument.GetReal());
            }
            else if (Argument.IsText())
            {
                const Text Content = Argument.GetText();
                Status = sqlite3_bind_text64(
                    Statement, Slot, Content.GetData(), Content.GetSize(), SQLITE_TRANSIENT, SQLITE_UTF8);
            }
            else
            {
                const ConstSpan<Byte> Content = Argument.GetBytes();
                Status = sqlite3_bind_blob64(Statement, Slot, Content.GetData(), Content.GetSize(), SQLITE_TRANSIENT);
            }

            if (Status != SQLITE_OK)
            {
                Outcome.Fail(StrConvert(sqlite3_errmsg(mHandle)));
                return false;
            }
        }
        return true;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void SQLite::Step(Ref<Plan> Known, Ref<Result> Outcome) const
    {
        const Ptr<sqlite3_stmt> Statement = Known.Compiled;
        const SInt32            Count     = sqlite3_column_count(Statement);

        if (Count > 0)
        {
            // The columns are told once and shared by every result after, until the database changes how many.
            if (!Known.Columns || Known.Columns->GetColumns() != Count)
            {
                Known.Columns = Retainer<Shape>::Create();

                for (SInt32 Column = 0; Column < Count; ++Column)
                {
                    Known.Columns->AddColumn(StrConvert(sqlite3_column_name(Statement, Column)));
                }
            }
            Outcome.SetShape(Known.Columns);
        }

        SInt32 Status = sqlite3_step(Statement);

        for (; Status == SQLITE_ROW; Status = sqlite3_step(Statement))
        {
            for (SInt32 Column = 0; Column < Count; ++Column)
            {
                switch (sqlite3_column_type(Statement, Column))
                {
                case SQLITE_INTEGER:
                    Outcome.AddCell(Value(sqlite3_column_int64(Statement, Column)));
                    break;
                case SQLITE_FLOAT:
                    Outcome.AddCell(Value(sqlite3_column_double(Statement, Column)));
                    break;
                case SQLITE_TEXT:
                    Outcome.AddCell(Value(Text(
                        reinterpret_cast<ConstPtr<Char>>(sqlite3_column_text(Statement, Column)),
                        sqlite3_column_bytes(Statement, Column))));
                    break;
                case SQLITE_BLOB:
                    Outcome.AddCell(Value(ConstSpan(
                        static_cast<ConstPtr<Byte>>(sqlite3_column_blob(Statement, Column)),
                        sqlite3_column_bytes(Statement, Column))));
                    break;
                default:
                    Outcome.AddCell(Value());
                    break;
                }
            }
        }

        if (Status == SQLITE_DONE)
        {
            Outcome.SetAffected(Count == 0 ? sqlite3_changes64(mHandle) : 0);
        }
        else
        {
            Outcome.Fail(StrConvert(sqlite3_errmsg(mHandle)));
        }
    }
}