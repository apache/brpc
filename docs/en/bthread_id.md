[中文版](../cn/bthread_id.md)

# bthread_id

`bthread_id` is a special synchronization structure. It can serialize different steps of an RPC, and it can find the RPC context (the Controller) in O(1) time. We are talking about `bthread_id_t` here, not `bthread_t` (the bthread tid). The name is unfortunate and easy to confuse.

Concretely, `bthread_id` solves:

- The response arriving while the request is still being sent, so response handling races with send code.
- A timer firing immediately after it is set, so timeout handling races with send code.
- Several responses from retries arriving at the same time and racing with each other.
- Finding the RPC context from a `correlation_id` in O(1) time, without a global hash map from `correlation_id` to context.
- Cancelling an RPC.

Those bugs show up widely in other RPC frameworks. Here is how brpc uses `bthread_id` to close them.

A `bthread_id` has two parts: a user-visible 64-bit id, and a hidden `bthread::Id` struct. User APIs all operate on the id. Mapping from id to struct is the same as [other structures](memory_management.md) in brpc: 32 bits are the pool offset, 32 bits are the version. The former locates in O(1); the latter avoids ABA.

The `bthread_id` API is not small:

- create
- lock
- unlock
- unlock_and_destroy
- join
- error

The extra APIs exist to cover different flows.

- Send request: `bthread_id_create` → `bthread_id_lock` → … register timer and send RPC … → `bthread_id_unlock`
- Receive response: `bthread_id_lock` → … process response → `bthread_id_unlock_and_destroy`
- Error handling: timeout / socket fail → `bthread_id_error` → run the `on_error` callback (which takes the lock). Then either:
  - Retry / backup request: register timer and send RPC again → `bthread_id_unlock`
  - Cannot retry, final failure: `bthread_id_unlock_and_destroy`
- Wait synchronously for the RPC to finish: `bthread_id_join`

To cut waiting, `bthread_id` has a few extra mechanisms:

- When an error arrives and the id is already locked, the error is pushed onto a pending queue and `bthread_id_error` returns immediately. On `bthread_id_unlock`, pending work is taken off the queue and run.
- When the RPC finishes and there is a user callback, first call `bthread_id_about_to_destroy` so in-flight `bthread_id_lock` waiters fail immediately, then run the user callback (which may take a long, unpredictable time), and finally `bthread_id_unlock_and_destroy`.
