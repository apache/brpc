
#ifndef BUTIL_MEMORY_ARENA_H
#define BUTIL_MEMORY_ARENA_H

#include "google/protobuf/message.h"

namespace butil {

template<class T>
class ArenaObjPtr {
public:
    explicit ArenaObjPtr(T *p, bool own) noexcept
        : _ptr(p), _own_ptr(own) {
    }

    ~ArenaObjPtr() {
        if (_ptr && _own_ptr) {
            delete _ptr;
            _ptr = nullptr;
        }
    }
    
private:
    T *_ptr;
    bool _own_ptr;
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
    ArenaObjPtr<google::protobuf::Message> CreateMessage(
            const google::protobuf::Message &proto_type) {
        google::protobuf::Message *msg = proto_type.New();
        return ArenaObjPtr<google::protobuf::Message>(msg, true);
    }

    template<class T>
    ArenaObjPtr<T> CreateMessage() {
        T *msg = new T;
        return ArenaObjPtr<T>(msg, true);
    }

    template<class T, class... Args>
    ArenaObjPtr<T> Create(Args&&... args) {
        T *obj = new T(std::forward<Args>(args)...);
        return ArenaObjPtr<T>(obj, true);
    }
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

    ArenaObjPtr<Def> CreateMessage(const Def &proto_type) {
        Def *msg = proto_type.New(_arena);
        if (_arena && !(msg->GetArena())) {
            // When cc_enable_arenas option equals to false
            _arena->Own(msg);
        }
        return ArenaObjPtr<Def>(msg, !_arena);
    }

    template<class T>
    ArenaObjPtr<T> CreateMessage() {
        T *msg = ArenaType::template CreateMessage<T>(_arena);
        if (_arena && !(msg->GetArena())) {
            // When cc_enable_arenas option equals to false
            _arena->Own(msg);
        }
        return ArenaObjPtr<T>(msg, !_arena);
    }

    template<class T, class... Args>
    ArenaObjPtr<T> Create(Args&&... args) {
        T *obj = ArenaType::template Create<T>(_arena, 
                                               std::forward<Args>(args)...);
        return ArenaObjPtr<T>(obj, !_arena);
    }

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

