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

#include "Postgres.hpp"
#include <Zyphryon.Base/System/Filesystem.hpp>

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// [   CODE   ]
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

namespace ZyPlugin::Database
{
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Postgres::Postgres(Text Address)
        : mAddress   { Address },
          mPort      { 5432 },
          mState     { State::Reaching },
          mPause     { kPause },
          mRetry     { 0.0 },
          mSending   { false },
          mReceiving { false },
          mSecure    { false },
          mVerified  { false },
          mAsked     { false },
          mOpened    { 0 },
          mNext      { 0 },
          mHead      { 0 },
          mNames     { 0 },
          mSent      { 0 },
          mAwake     { false },
          mRoused    { false },
          mThread    { Thread::Work::Create<& Postgres::Run>(this) }
    {
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Parse()
    {
        // The address reads `user:password@host:port/name?options`, split at the last `@` so a password may hold one.
        const Text Whole = mAddress;
        const SInt At    = StrFindLast(Whole, '@');
        const Text Who   = (At != -1 ? Whole.Slice(0, static_cast<UInt>(At)) : Whole);
        const Text Where = (At != -1 ? Whole.Slice(static_cast<UInt>(At) + 1) : Text::Empty());

        Text Secret;
        Text Asked;
        Text Options;
        Text Port;

        mUser     = Part(Who, ':', Secret);
        mPassword = Secret;
        mHost     = Part(Part(Where, '/', Asked), ':', Port);
        mName     = Part(Asked, '?', Options);

        if (mName.IsEmpty())
        {
            mName = mUser;
        }
        if (UInt Cursor = 0; !Port.IsEmpty())
        {
            mPort = StrExtractNumber<10, UInt16>(Port, Cursor);
        }

        // The options are parted by `&`, and say whether the link is encrypted and which authority it trusts.
        StrSplit(Options, '&', [this](Text Option)
        {
            mSecure   = mSecure   || Option == "tls=on" || Option == "tls=verified";
            mVerified = mVerified || Option == "tls=verified";

            if (StrStartsWith(Option, "ca="))
            {
                // The authority's certificates are read whole here, since the link is begun again on every mend.
                Blob File;

                if (Filesystem::Read(Option.Slice(3), File) == Filesystem::Result::Success)
                {
                    mAuthority.Clear();
                    mAuthority.Append(File);
                }
                else
                {
                    LOG_E("Database: The authority file '{0}' could not be read", Option.Slice(3));
                }
            }
        });
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Postgres::~Postgres()
    {
        mThread.Stop();

        if (mAwake.load(std::memory_order_acquire))
        {
            mProactor.Rouse();
        }
        mThread.Join();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Send(AnyRef<Sequence<Query>> Requests, AnyRef<Answer> Callback)
    {
        {
            const Guard Hold(mMutex);

            mOrders.Append(Move(Requests), Move(Callback));
        }

        // The thread is woken once for all that is sent before it next looks, not once for each.
        if (mAwake.load(std::memory_order_acquire) && !mRoused.exchange(true, std::memory_order_acq_rel))
        {
            mProactor.Rouse();
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Run(ConstRef<Thread::Token> Token)
    {
        Parse();
        mChunk.Resize(kChunk);

        ZyNetwork::Socket::Initialize();

        const Bool Started = mProactor.Start();
        mAwake.store(Started, std::memory_order_release);

        if (!Started || !Reach())
        {
            Break("The server could not be reached for");
        }

        while (true)
        {
            Bool Stopping;
            {
                const Guard Hold(mMutex);

                for (UInt Index = 0; Index < mOrders.GetSize(); ++Index)
                {
                    mWaiting.Append(Move(mOrders[Index]));
                }
                mOrders.Clear();
                mRoused.store(false, std::memory_order_release);
                Stopping = Token.Stopped();
            }

            Dispatch();
            Flush();

            if (Stopping && mWaiting.IsEmpty() && mHead == mFlights.GetSize())
            {
                break;
            }

            if (mState == State::Broken)
            {
                Mend();
            }

            for (ConstRef<ZyNetwork::Proactor::Event> Event : mProactor.Wait(kIdle))
            {
                switch (Event.Kind)
                {
                case ZyNetwork::Operation::Connect:
                    if (Event.Cause != ZyNetwork::Reason::None)
                    {
                        Break("The server refused the link or could not be reached");
                    }
                    else if (mState == State::Reaching && mSecure)
                    {
                        // Asked for before anything else, as eight bytes that are a length and a number of their own.
                        static constexpr Array<Byte, 8> kRequest = { 0, 0, 0, 8, 0x04, 0xD2, 0x16, 0x2F };

                        mState = State::Securing;
                        mAsked = true;
                        mRaw.Append(kRequest);
                        Flush();
                        Listen();
                    }
                    else if (mState == State::Reaching)
                    {
                        mState = State::Greeting;
                        Greet();
                        Listen();
                    }
                    break;
                case ZyNetwork::Operation::Send:
                    mSending = false;

                    if (Event.Cause != ZyNetwork::Reason::None)
                    {
                        Break("The link to the server was lost while writing");
                    }
                    else if (Event.Size < mWire.GetSize())
                    {
                        mWire.Remove(0, Event.Size);
                    }
                    else
                    {
                        mWire.Clear();
                    }
                    break;
                case ZyNetwork::Operation::Receive:
                    mReceiving = false;

                    if (Event.Cause != ZyNetwork::Reason::None || Event.Size == 0)
                    {
                        Break("The server closed the link");
                    }
                    else if (mState != State::Broken)
                    {
                        Take(ConstSpan(mChunk.GetData(), Event.Size));
                        Listen();
                    }
                    break;
                default:
                    break;
                }
            }
        }

        // The platform has to give back whatever is still out before the socket and the queue are closed.
        if (mSocket.IsValid())
        {
            mProactor.Cancel(mSocket);

            for (UInt32 Pass = 0; Pass < 50 && (mSending || mReceiving); ++Pass)
            {
                for (ConstRef<ZyNetwork::Proactor::Event> Event : mProactor.Wait(20))
                {
                    mSending   = mSending   && Event.Kind != ZyNetwork::Operation::Send;
                    mReceiving = mReceiving && Event.Kind != ZyNetwork::Operation::Receive;
                }
            }
            mSocket.Close();
        }
        mProactor.Stop();

        ZyNetwork::Socket::Teardown();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Bool Postgres::Reach()
    {
        ZyNetwork::Address  Server;
        ZyNetwork::Endpoint Resolved;

        Server.Host = mHost;
        Server.Port = mPort;

        if (!ZyNetwork::Endpoint::Resolve(Server, ZyNetwork::Transport::TCP, Resolved))
        {
            return false;
        }

        if (!mSocket.Open(ZyNetwork::Transport::TCP, Resolved))
        {
            return false;
        }

        if (!mProactor.Attach(mSocket) || !mProactor.Connect(mSocket, ZyNetwork::Connection(), Resolved))
        {
            mSocket.Close();
            return false;
        }
        return true;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Greet()
    {
        Open(0);
        Put32(196608);
        Put("user", true);
        Put(mUser, true);
        Put("database", true);
        Put(mName, true);
        Put("client_encoding", true);
        Put("UTF8", true);
        Put(Text::Empty(), true);
        Close();
        Flush();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Authenticate(ConstSpan<Byte> Body)
    {
        if (Body.GetSize() < 4)
        {
            Break("The server sent an authentication message too short to read");
            return;
        }

        const UInt32 Code = Peek(Body.GetData());
        const Text   Rest(reinterpret_cast<ConstPtr<Char>>(Body.GetData()) + 4, Body.GetSize() - 4);

        switch (Code)
        {
        case 0:
            break;
        case 3:
            Open('p');
            Put(mPassword, true);
            Close();
            break;
        case 10:
        {
            const Str First = mScram.Begin();

            Open('p');
            Put("SCRAM-SHA-256", true);
            Put32(static_cast<UInt32>(First.GetSize()));
            Put(First, false);
            Close();
            break;
        }
        case 11:
        {
            Str Final;

            if (!mScram.Continue(Rest, mPassword, Final))
            {
                Break("The server's challenge could not be answered");
                return;
            }
            Open('p');
            Put(Final, false);
            Close();
            break;
        }
        case 12:
            if (!mScram.Verify(Rest))
            {
                Break("The server could not prove it knows the password");
                return;
            }
            break;
        default:
            Break("The server asks for a way of authenticating this link does not speak");
            return;
        }
        Flush();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Dispatch()
    {
        if (mState == State::Broken)
        {
            for (; mNext < mWaiting.GetSize(); ++mNext)
            {
                Deliver(Move(mWaiting[mNext].Callback), Refuse(mWaiting[mNext].Requests.GetSize(), mCause));
            }
        }
        else if (mState == State::Ready && mFlights.GetSize() - mHead <= kWindow / 2)
        {
            // A window that filled is topped up once half of it is answered, so its queries leave many to a write.
            for (; mNext < mWaiting.GetSize() && mFlights.GetSize() - mHead < kWindow; ++mNext)
            {
                Encode(mWaiting[mNext]);
            }
        }

        // What went out is let go once enough of it gathers, so a queue that never runs dry does not keep every query.
        if (mNext >= kWindow || (mNext > 0 && mNext == mWaiting.GetSize()))
        {
            mWaiting.Remove(0, mNext);
            mNext = 0;
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Encode(Ref<Order> Entry)
    {
        const ConstSpan<Query> Requests = Entry.Requests;
        Sequence<UInt32>       Names;
        UInt32                 First    = 0;
        UInt32                 Fresh    = 0;

        // The answered flights at the front are let go once enough of them gather.
        if (mHead >= kWindow)
        {
            mFlights.Remove(0, mHead);
            mHead = 0;
        }

        if (Requests.GetSize() == 1 && Requests[0].GetArguments().IsEmpty() && IsSimple(Requests[0].GetStatement()))
        {
            // A lone text of several statements, or sent for the first time, is compiled by the server and let go.
            Open('Q');
            Put(Requests[0].GetStatement(), true);
            Close();
        }
        else
        {
            // Every statement new to the server is compiled before the first runs, so a query refused later skips none.
            for (UInt Index = 0; Index < Requests.GetSize(); ++Index)
            {
                const Text             Statement = Requests[Index].GetStatement();
                const ConstPtr<UInt32> Known     = mPrepared.Find(Statement);
                const UInt32           Name      = (Known ? * Known : mNames);

                if (!Known)
                {
                    Forget();

                    ++mNames;
                    First = (Fresh++ == 0 ? Name : First);
                    mPrepared.Assign(Str(Statement), Name);

                    Open('P');
                    Tag(Name);
                    Put(Statement, true);
                    Put16(0);
                    Close();
                }
                mPlans.FindOrInsert(Name).Used = mSent;
                Names.Append(Name);
            }

            // Values go as text and rows come back as text, so the server reads each into the type it expects.
            for (UInt Index = 0; Index < Requests.GetSize(); ++Index)
            {
                const ConstSpan<Value> Arguments = Requests[Index].GetArguments();

                Open('B');
                Put(Text::Empty(), true);
                Tag(Names[Index]);
                Put16(0);
                Put16(static_cast<UInt16>(Arguments.GetSize()));

                for (UInt Slot = 0; Slot < Arguments.GetSize(); ++Slot)
                {
                    Put(Arguments[Slot]);
                }
                Put16(0);
                Close();

                // The rows are described once for a statement, and every later query of it takes what was told then.
                if (!mPlans.Find(Names[Index])->Described)
                {
                    Open('D');
                    Put("P", true);
                    Close();
                }

                Open('E');
                Put(Text::Empty(), true);
                Put32(0);
                Close();
            }

            // One sync closes the batch, so the server keeps what the queries changed only when every one of them ran.
            Open('S');
            Close();
        }

        Sequence<Result> Outcomes(Requests.GetSize());
        Outcomes.Advance(Requests.GetSize());

        mFlights.Append(Move(Names), Move(Entry.Callback), Move(Outcomes), 0u, First, Fresh);
        ++mSent;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Forget()
    {
        // A statement a batch in the air still runs is kept, and the bound is outgrown until that batch is answered.
        const UInt64 Settled = mSent - (mFlights.GetSize() - mHead);

        while (mPlans.GetSize() >= kKeep)
        {
            UInt64 Oldest = kMaximum<UInt64>;
            UInt32 Name   = 0;

            mPlans.ForEach([& Oldest, & Name](UInt32 Known, ConstRef<Plan> Told)
            {
                if (Told.Used < Oldest)
                {
                    Oldest = Told.Used;
                    Name   = Known;
                }
            });

            if (Oldest >= Settled)
            {
                return;
            }

            mPlans.Erase(Name);
            mPrepared.EraseIf([Name](ConstRef<Str>, UInt32 Known)
            {
                return Known == Name;
            });

            // The server is told to let go of it too, among the messages of the batch being written.
            Open('C');
            Put("S", false);
            Tag(Name);
            Close();
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Bool Postgres::IsSimple(Text Statement)
    {
        // A semicolon before the end may part several statements, where a quoted one sends the text this way too.
        const Text Body = StrTrimRight(Statement);

        if (StrFind(Body.IsEmpty() || Body.GetBack() != ';' ? Body : Body.Slice(0, Body.GetSize() - 1), ';') != -1)
        {
            return true;
        }

        if (mPrepared.Find(Statement))
        {
            return false;
        }

        // A text is kept from its second use, so one sent once is compiled and let go.
        if (mSeen.GetSize() >= kSeen)
        {
            mSeen.Clear();
        }
        return mSeen.Insert(Statement.Hash());
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Flush()
    {
        if (mSending || mState == State::Broken || mState == State::Reaching)
        {
            return;
        }

        if (mWire.IsEmpty())
        {
            // What the encryption itself has to say goes as it is and first, and the messages go sealed after it.
            if (!mRaw.IsEmpty())
            {
                mWire = Move(mRaw);
                mRaw.Clear();
            }
            else if (mQueue.IsEmpty())
            {
                return;
            }
            else if (!mSecure)
            {
                mWire = Move(mQueue);
                mQueue.Clear();
            }
            else if (mTunnel.Seal(mQueue, mWire))
            {
                mQueue.Clear();
            }
            else
            {
                Break("The encrypted link broke while writing");
                return;
            }
        }

        if (mProactor.Send(mSocket, ZyNetwork::Connection(), mWire))
        {
            mSending = true;
        }
        else
        {
            Break("The link to the server was lost while writing");
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Listen()
    {
        if (mReceiving || mState == State::Broken)
        {
            return;
        }

        if (mProactor.Receive(mSocket, ZyNetwork::Connection(), mChunk))
        {
            mReceiving = true;
        }
        else
        {
            Break("The link to the server was lost while reading");
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Take(ConstSpan<Byte> Data)
    {
        if (!mSecure)
        {
            Absorb(Data);
            return;
        }

        mPlain.Clear();

        if (mAsked)
        {
            // The server answers the request with one letter, `S` when it will encrypt, and nothing may follow it.
            mAsked = false;

            if (Data.GetSize() != 1 || Data[0] != 'S' || !mTunnel.Begin(mHost, mVerified, mAuthority, mRaw))
            {
                Break("The server does not take an encrypted link");
                return;
            }
        }
        else if (!mTunnel.Feed(Data, mPlain, mRaw))
        {
            Break(mTunnel.GetState() == TLS::State::Closed
                ? Text("The server closed the link")
                : Text("The link could not be encrypted, or the server's certificate was refused"));
            return;
        }

        // Once the keys are agreed the server is told who asks, as it would be on a plain link.
        if (mState == State::Securing && mTunnel.GetState() == TLS::State::Open)
        {
            mState = State::Greeting;
            Greet();
        }
        Absorb(mPlain);
        Flush();
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Absorb(ConstSpan<Byte> Data)
    {
        // Whole messages are read where they arrived, and only a message cut off is kept for the rest of it.
        const Bool Kept = !mInbound.IsEmpty();

        if (Kept)
        {
            mInbound.Append(Data);
        }

        const ConstSpan<Byte> Pending = (Kept ? ConstSpan<Byte>(mInbound) : Data);

        // Each message is a letter, its length counting itself, and its body.
        UInt Cursor = 0;

        while (mState != State::Broken && Pending.GetSize() - Cursor >= 5)
        {
            const ConstPtr<Byte> Head   = Pending.GetData() + Cursor;
            const UInt32         Length = Peek(Head + 1);

            if (Length < 4)
            {
                Break("The server sent a message this link cannot read");
                break;
            }
            if (Pending.GetSize() - Cursor < 1 + static_cast<UInt>(Length))
            {
                break;
            }

            Handle(static_cast<Char>(Head[0]), ConstSpan(Head + 5, Length - 4));
            Cursor += 1 + Length;
        }

        if (mState == State::Broken)
        {
            return;
        }

        if (Kept)
        {
            mInbound.Remove(0, Cursor);
        }
        else
        {
            mInbound.Append(ConstSpan(Data.GetData() + Cursor, Data.GetSize() - Cursor));
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Handle(Char Kind, ConstSpan<Byte> Body)
    {
        const ConstPtr<Byte> Data = Body.GetData();
        const UInt           Size = Body.GetSize();
        const Text           Content(reinterpret_cast<ConstPtr<Char>>(Data), Size);

        if (mState == State::Greeting)
        {
            if (Kind == 'R')
            {
                Authenticate(Body);
            }
            else if (Kind == 'Z')
            {
                mState = State::Ready;
                mPause = kPause;

                if (!mCause.IsEmpty())
                {
                    LOG_I("Database: The link to '{0}' is back", mHost);

                    mCause.Clear();
                }
            }
            else if (Kind == 'E')
            {
                Break(Reason(Body, "The server refused the user"));
            }
            return;
        }

        if (mHead >= mFlights.GetSize())
        {
            return;
        }

        Ref<Flight> Current = mFlights[mHead];

        switch (Kind)
        {
        case '1':
            ++Current.First;
            --Current.Fresh;
            break;
        case '2':
            // A statement described before was not asked again, so its rows take the columns the server told then.
            if (const Ptr<Plan> Known = Recall(Current); Known && Known->Columns)
            {
                Current.Outcomes[Current.Cursor].SetShape(Known->Columns);
                mTypes = Known->Types;
            }
            break;
        case 'n':
            if (const Ptr<Plan> Known = Recall(Current))
            {
                Known->Described = true;
            }
            break;
        case 'T':
        {
            // Every column is its name, then eighteen bytes of which the type takes four from the seventh.
            const UInt32          Columns = (Size >= 2 ? (static_cast<UInt32>(Data[0]) << 8) | Data[1] : 0);
            UInt                  Cursor  = 2;
            Ref<Result>           Outcome = Current.Outcomes[Current.Cursor];
            const Retainer<Shape> Told    = Retainer<Shape>::Create();

            mTypes.Clear();

            for (UInt32 Column = 0; Column < Columns && Cursor < Size; ++Column)
            {
                const UInt Start = Cursor;

                while (Cursor < Size && Data[Cursor] != 0)
                {
                    ++Cursor;
                }
                if (Cursor + 19 > Size)
                {
                    break;
                }
                Told->AddColumn(Content.Slice(Start, Cursor - Start));

                mTypes.Append(Peek(Data + Cursor + 7));
                Cursor += 19;
            }
            Outcome = Result();
            Outcome.SetShape(Told);

            // What the server told is kept with the statement, so its later queries do not ask again.
            if (const Ptr<Plan> Known = Recall(Current))
            {
                Known->Described = true;
                Known->Columns   = Told;
                Known->Types     = mTypes;
            }
            break;
        }
        case 'D':
        {
            const UInt32 Columns = (Size >= 2 ? (static_cast<UInt32>(Data[0]) << 8) | Data[1] : 0);
            UInt         Cursor  = 2;
            Ref<Result>  Outcome = Current.Outcomes[Current.Cursor];

            for (UInt32 Column = 0; Column < Columns && Cursor + 4 <= Size && Outcome.IsOk(); ++Column)
            {
                const UInt32 Length = Peek(Data + Cursor);
                Cursor += 4;

                if (Length == kMaximum<UInt32> || Cursor + Length > Size)
                {
                    Outcome.AddCell(Value());
                }
                else
                {
                    const UInt32 Type = (Column < mTypes.GetSize() ? mTypes[Column] : 0);

                    Outcome.AddCell(Read(Type, Content.Slice(Cursor, Length)));
                    Cursor += Length;
                }
            }
            break;
        }
        case 'C':
        {
            Ref<Result> Outcome = Current.Outcomes[Current.Cursor];

            if (mTypes.IsEmpty() && Outcome.IsOk())
            {
                // The tag ends in the number of rows changed, as `UPDATE 5` or `INSERT 0 5`, or in no number.
                UInt Cursor = static_cast<UInt>(StrFindLast(Content, ' ') + 1);

                Outcome = Result();
                Outcome.SetAffected(StrExtractNumber<10, UInt64>(Content, Cursor));
            }
            mTypes.Clear();

            // Each query of a batch ends here, and the answers after it fill the next one's result.
            if (Current.Cursor + 1 < Current.Outcomes.GetSize())
            {
                ++Current.Cursor;
            }
            break;
        }
        case 'E':
            Current.Outcomes = Refuse(Current.Outcomes.GetSize(), Reason(Body, "The server refused the query"));

            // The statements the server did not get to compile are forgotten, so the next query compiles them afresh.
            mPrepared.EraseIf([& Current](ConstRef<Str>, UInt32 Name)
            {
                return Name >= Current.First && Name < Current.First + Current.Fresh;
            });
            Current.Fresh = 0;
            break;
        case 'Z':
            Deliver(Move(Current.Callback), Move(Current.Outcomes));
            mTypes.Clear();
            ++mHead;
            break;
        default:
            break;
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Ptr<Postgres::Plan> Postgres::Recall(ConstRef<Flight> Current)
    {
        return (Current.Cursor < Current.Names.GetSize() ? mPlans.Find(Current.Names[Current.Cursor]) : nullptr);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Break(Text Cause)
    {
        if (mState == State::Broken)
        {
            return;
        }

        LOG_E("Database: The link to '{0}' broke, {1}", mHost, Cause);

        mState = State::Broken;
        mCause = Cause;

        for (; mHead < mFlights.GetSize(); ++mHead)
        {
            Deliver(Move(mFlights[mHead].Callback), Refuse(mFlights[mHead].Outcomes.GetSize(), Cause));
        }
        mQueue.Clear();

        // Whatever the lost link still has out is called back, and each attempt waits twice as long as the last.
        mProactor.Cancel(mSocket);

        mRetry = mClock.GetSeconds() + mPause;
        mPause = (mPause * 2.0 < kLimit ? mPause * 2.0 : kLimit);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Mend()
    {
        if (mSending || mReceiving || mClock.GetSeconds() < mRetry)
        {
            return;
        }

        // The server knows nothing of the lost link, so the new one starts with no statement compiled and nothing read.
        mSocket.Close();
        mInbound.Clear();
        mRaw.Clear();
        mWire.Clear();
        mQueue.Clear();
        mFlights.Clear();
        mTypes.Clear();
        mPrepared.Clear();
        mPlans.Clear();

        mOpened = 0;
        mHead   = 0;
        mNames  = 0;
        mAsked  = false;
        mState  = State::Reaching;

        if (!Reach())
        {
            Break("The server could not be reached for");
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Open(Char Kind)
    {
        if (Kind != 0)
        {
            mQueue.Append(static_cast<Byte>(Kind));
        }
        mOpened = mQueue.GetSize();

        Put32(0);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Close()
    {
        Poke(mOpened, static_cast<UInt32>(mQueue.GetSize() - mOpened));
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Put16(UInt16 Number)
    {
        mQueue.Append(static_cast<Byte>(Number >> 8));
        mQueue.Append(static_cast<Byte>(Number));
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Put32(UInt32 Number)
    {
        mQueue.Advance(4);
        Poke(mQueue.GetSize() - 4, Number);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Put(Text Content, Bool Closed)
    {
        mQueue.Append(Content.GetBytes());

        if (Closed)
        {
            mQueue.Append(static_cast<Byte>(0));
        }
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Put(ConstRef<Value> Argument)
    {
        // A value goes as text behind its length, which is written once the text is, and nothing goes as -1 alone.
        const UInt Start = mQueue.GetSize();

        Put32(kMaximum<UInt32>);

        if (Argument.IsNull())
        {
            return;
        }

        if (Argument.IsInteger())
        {
            Str32 Digits;
            Digits.Format("{0}", Argument.GetInteger());
            Put(Digits, false);
        }
        else if (Argument.IsReal())
        {
            // Seventeen digits are what a 64-bit real needs to be read back as the same number.
            Str32 Digits;
            StrWriteReal(Digits, Argument.GetReal(), 17);
            Put(Digits, false);
        }
        else if (Argument.IsText())
        {
            Put(Argument.GetText(), false);
        }
        else
        {
            // Bytes go as `\x` and two hexadecimal digits each.
            static constexpr Text kDigits = "0123456789abcdef";

            Put("\\x", false);

            for (const Byte Each : Argument.GetBytes())
            {
                mQueue.Append(static_cast<Byte>(kDigits[Each >> 4]));
                mQueue.Append(static_cast<Byte>(kDigits[Each & 15]));
            }
        }
        Poke(Start, static_cast<UInt32>(mQueue.GetSize() - Start - 4));
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    void Postgres::Tag(UInt32 Name)
    {
        Str32 Label;
        Label.Format("s{0}", Name);
        Put(Label, true);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Text Postgres::Reason(ConstSpan<Byte> Body, Text Fallback)
    {
        const ConstPtr<Char> Data = reinterpret_cast<ConstPtr<Char>>(Body.GetData());
        const UInt           Size = Body.GetSize();

        // An error is fields, each a letter and text closed by a zero, and the reason is the field lettered `M`.
        for (UInt Index = 0; Index < Size && Data[Index] != 0;)
        {
            const UInt Start = Index + 1;
            UInt       End   = Start;

            while (End < Size && Data[End] != 0)
            {
                ++End;
            }
            if (Data[Index] == 'M')
            {
                return Text(Data + Start, End - Start);
            }
            Index = End + 1;
        }
        return Fallback;
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Text Postgres::Part(Text Whole, Char Mark, Ref<Text> Tail)
    {
        const SInt Index = StrFind(Whole, Mark);

        Tail = (Index != -1 ? Whole.Slice(static_cast<UInt>(Index) + 1) : Text::Empty());
        return (Index != -1 ? Whole.Slice(0, static_cast<UInt>(Index)) : Whole);
    }

    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
    // -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

    Value Postgres::Read(UInt32 Type, Text Cell)
    {
        switch (Type)
        {
        case 16:
            return Value(Cell.GetSize() > 0 && Cell[0] == 't' ? 1 : 0);
        case 20:
        case 21:
        case 23:
        case 26:
        {
            UInt Cursor = 0;
            return Value(StrExtractNumber<10, SInt64>(Cell, Cursor));
        }
        case 700:
        case 701:
        case 1700:
        {
            UInt Cursor = 0;
            return Value(StrExtractNumber<Real64>(Cell, Cursor));
        }
        case 17:
        {
            Sequence<Byte> Bytes;

            for (UInt Index = 2; Index + 1 < Cell.GetSize(); Index += 2)
            {
                const Char High = Cell[Index];
                const Char Low  = Cell[Index + 1];

                Bytes.Append(static_cast<Byte>(((High <= '9' ? High - '0' : (High | 32) - 'a' + 10) << 4)
                                              | (Low  <= '9' ? Low  - '0' : (Low  | 32) - 'a' + 10)));
            }
            return Value(Bytes);
        }
        default:
            return Value(Cell);
        }
    }
}