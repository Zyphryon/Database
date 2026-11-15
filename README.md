# 🌌 Zyphryon Database
*Save games and servers, without blocking a frame.*

**Database** is the storage layer of the [Zyphryon Engine](https://github.com/Zyphryon/Engine)

---

## 🚀 Getting started

Add the repository to your CMake project and link `ZyDatabase`:

```cmake
FetchContent_Declare(ZyDatabase GIT_REPOSITORY https://github.com/Zyphryon/Database GIT_TAG main)
FetchContent_MakeAvailable(ZyDatabase)

TARGET_LINK_LIBRARIES(MyGame PRIVATE ZyDatabase)
```

| Option | Default | Builds |
|--------|---------|--------|
| `ZY_DATABASE_SQLITE` | `ON` | The SQLite link, with SQLite 3.54.1 compiled in |
| `ZY_DATABASE_POSTGRES` | `ON` | The PostgreSQL link |

Open a link, send queries, and poll once a frame:

```cpp
#include <Database/Database.hpp>

using namespace ZyPlugin::Database;

const Unique<Connection> Link = Database::Open("sqlite://save.db");

Link->Submit(Query("INSERT INTO hero (name, level) VALUES ($1, $2)", "Aldric", 12), Connection::Reply());

Link->Submit(Query("SELECT name, level FROM hero WHERE level >= $1", 10), Connection::Reply([](ConstRef<Result> Outcome)
{
    for (UInt32 Row = 0; Row < Outcome.GetRows(); ++Row)
    {
        LOG_I("{0} is level {1}", Outcome.GetValue(Row, 0).GetText(), Outcome.GetValue(Row, 1).GetInteger());
    }
}));

// Once a frame, on the game thread, every result that arrived is handed to its callback.
Link->Poll();
```

A batch runs whole or not at all:

```cpp
Sequence<Query> Trade;
Trade.Append("UPDATE purse SET gold = gold - $1 WHERE owner = $2", 50, Buyer);
Trade.Append("INSERT INTO pack (owner, item) VALUES ($1, $2)", Buyer, "Sword");

Link->Submit(Move(Trade), Connection::Answer([](ConstSpan<Result> Outcomes)
{
    // One result per query, every one failed with the same reason when the batch was rolled back.
}));
```

### Addresses

| Address | Opens |
|---------|-------|
| `sqlite://save.db` | A database kept in one file, created when missing |
| `sqlite://:memory:` | A database that lives only as long as the link |
| `postgres://user:password@host:5432/name` | A PostgreSQL database, over a plain link |
| `postgres://user:password@host/name?tls=on` | The same, encrypted |
| `postgres://user:password@host/name?tls=verified&ca=ca.pem` | Encrypted, refusing a server the authority did not sign |

---

## 📄 License

Zyphryon Database is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

---

## 🤝 Contributing

Contributions are welcome. Expect:

- **Code reviews** focused on correctness, since a game's saves and players live behind this library
- **Measurements** with any change that claims to be faster, run against both databases
- **Robustness** — anything that reads from a server must survive a corrupted or malicious one
- **Documentation** for all public APIs, following the engine's conventions

Feel free to submit Pull Requests, open Issues, or discuss new features in Discussions.

---

*Built with ❤️ alongside the Zyphryon Engine.*