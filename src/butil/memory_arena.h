
#ifndef BUTIL_MEMORY_ARENA_H
#define BUTIL_MEMORY_ARENA_H

#include <memory>
#include "google/protobuf/message.h"
#include "butil/logging.h"

namespace butil {

template<class T>
struct ArenaObjDeleter {
    constexpr ArenaObjDeleter() noexcept = default;

    ArenaObjDeleter(bool own) noexcept
        : _own_obj(own) {
    }

    void operator()(T *ptr) const {
        if (_own_obj) {
            VLOG(199) << "delete!";
            delete ptr;
        }
    }

private:
    bool _own_obj;
};

namespace internal {

// Template meta to check protobuf version.
//   protobuf 3.x: ArenaCheck::test<T>(0) => int8_t
//   protobuf 2.x: ArenaCheck::test<T>(0) => int64_t
struct ArenaCheck {
  template<class T> static int8_t test(decltype(&T::GetArena));
  template<class T> static int64_t test(...);
};

// Don't specialize by yourselves, MemoryArenaDefault and MemoryArenaSimple
// will be enough.
// A simple implementation.
template<class Def, class Enable = void>
class MemoryArena {
public:
    std::unique_ptr<
        google::protobuf::Message, 
        ArenaObjDeleter<google::protobuf::Message>
    > 
    CreateMessage(const google::protobuf::Message &proto_type) {
        google::protobuf::Message *msg = proto_type.New();
        VLOG(199) << __FUNCTION__;
        return std::unique_ptr<
                    google::protobuf::Message, 
                    ArenaObjDeleter<google::protobuf::Message>
               >(msg, ArenaObjDeleter<google::protobuf::Message>(true));
    }

    template<class T>
    std::unique_ptr<T, ArenaObjDeleter<T>> CreateMessage() {
        T *msg = new T;
        return std::unique_ptr<T, ArenaObjDeleter<T>>
                (msg, ArenaObjDeleter<T>(true));
    }

    template<class T, class... Args>
    std::unique_ptr<T, ArenaObjDeleter<T>> Create(Args&&... args) {
        T *obj = new T(std::forward<Args>(args)...);
        return std::unique_ptr<T, ArenaObjDeleter<T>>
                (obj, ArenaObjDeleter<T>(true));
    }

    bool OwnObject() { return false; }
};

// Protobuf arena is encapsulated.
template<class Def>
class MemoryArena<Def, 
    typename std::enable_if<
        sizeof(ArenaCheck::test<Def>(0)) == sizeof(int8_t)
    >::type
> {
public:
    typedef decltype(((Def*)nullptr)->GetArena())               ArenaTypePtr;
    typedef typename std::remove_pointer<ArenaTypePtr>::type    ArenaType;

    MemoryArena(bool use_arena = true)
        : _arena(nullptr) {
        if (use_arena) {
            _arena = new ArenaType;
        }
    }

    ~MemoryArena() {
        if (_arena) {
            delete _arena;
            _arena = nullptr;
        }
    }

    std::unique_ptr<Def, ArenaObjDeleter<Def>>
    CreateMessage(const Def &proto_type) {
        // If cc_enable_arenas option equals to false, msg is owned by arena
        // inside New, but msg->GetArena will return NULL.
        Def *msg = proto_type.New(_arena);
        VLOG(199) << __FUNCTION__;
        return std::unique_ptr<Def, ArenaObjDeleter<Def>>
                (msg, ArenaObjDeleter<Def>(!_arena));
    }

    template<class T>
    std::unique_ptr<T, ArenaObjDeleter<T>> CreateMessage() {
        // Compile error if cc_enable_arenas option equals to false
        T *msg = ArenaType::template CreateMessage<T>(_arena);
        return std::unique_ptr<T, ArenaObjDeleter<T>>
                (msg, ArenaObjDeleter<T>(!_arena));
    }

    template<class T, class... Args>
    std::unique_ptr<T, ArenaObjDeleter<T>> Create(Args&&... args) {
        T *obj = ArenaType::template Create<T>(_arena, 
                                               std::forward<Args>(args)...);
        return std::unique_ptr<T, ArenaObjDeleter<T>>
                (obj, ArenaObjDeleter<T>(!_arena));
    }

    bool OwnObject() { return _arena; }

private:
    ArenaTypePtr _arena;
};

} // namespace internal

// MemoryArenaDefault could be used as a memory management, whose 
// implementation is not cared, e.g. in the ProcessRpcRequest. Using 
// protobuf arena has higher priority. 
// Specialization table:
//   + - - - - - - - + - - - - - - - - - - - - - - - - +
//   | protobuf 2.x  | The simple implementation       |
//   + - - - - - - - + - - - - - - - - - - - - - - - - +
//   | protobuf 3.x  | Encapsulating protobuf arena    |
//   + - - - - - - - + - - - - - - - - - - - - - - - - +
typedef internal::MemoryArena<google::protobuf::Message> MemoryArenaDefault;

// MemoryArenaSimple is our simple implementation, no matter protobuf 2.x
// or protobuf 3.x.
// Specialization table:
//   + - - - - - - - + - - - - - - - - - - - - - - - - +
//   | protobuf 2.x  | The simple implementation       |
//   + - - - - - - - + - - - - - - - - - - - - - - - - +
//   | protobuf 3.x  | The simple implementation       |
//   + - - - - - - - + - - - - - - - - - - - - - - - - +
typedef internal::MemoryArena<void>                      MemoryArenaSimple;

} // namespace butil

#endif // BUTIL_MEMORY_ARENA_H

