#### Task Structure

A **Task** is a unit of concurrent execution with its own VM state:

```c
typedef enum {
    TASK_READY,       // Ready to run
    TASK_RUNNING,     // Currently executing
    TASK_YIELDED,     // Voluntarily yielded, ready to resume
    TASK_BLOCKED,     // Blocked on I/O or FFI call
    TASK_COMPLETED,   // Finished successfully
    TASK_FAILED       // Terminated with unhandled exception
} TaskState;

struct Task {
    // Unique identifier
    uint64_t       id;

    // Task state
    TaskState      state;

    // VM state (entire execution context)
    Value*         stack;           // Value stack
    int            stack_size;      // Allocated stack size
    int            stack_pointer;   // Current stack top
    int            frame_pointer;   // Current frame base
    uint32_t       pc;              // Program counter
    Handler*       exception_handler; // Exception handler chain

    // Scheduling
    uint64_t       time_slice;      // Instructions executed
    uint64_t       total_instructions; // Lifetime instruction count
    int            priority;        // Task priority (0-255)

    // Blocking state
    void*          blocked_on;      // Resource task is blocked on (or NULL)

    // Task-local storage
    HashMap*       thread_locals;   // Dynamic var bindings

    // Parent/child relationships (for task groups)
    Task*          parent;
    Vector*        children;

    // Result or error
    Value          result;          // Return value or exception
};
```

#### Scheduler Structure

The **Scheduler** manages all tasks and coordinates execution across OS threads:

```c
struct Scheduler {
    // Thread pool
    pthread_t*     threads;         // OS thread pool
    int            n_threads;       // Number of OS threads

    // Task queues (per priority level)
    Queue*         ready_queues[256]; // Ready tasks by priority
    Queue*         blocked_queue;   // Blocked tasks

    // Global state
    pthread_mutex_t lock;           // Protect scheduler state
    pthread_cond_t  work_available; // Signal threads when work available
    bool           shutdown;        // Shutdown flag

    // Statistics
    uint64_t       tasks_created;
    uint64_t       tasks_completed;
    uint64_t       context_switches;

    // Configuration
    uint32_t       time_slice_instructions; // Instructions per time slice
    bool           work_stealing_enabled;
};
```

#### Task Creation and Spawning

**spawn instruction or function:**

```clojure
;; Spawn a new task
(spawn (fn []
         (println "Running in separate task")
         (some-computation)))

;; Returns a task handle (future-like)
(let [task (spawn (fn [] (+ 1 2)))]
  (await task))  ; Block until task completes, get result
```

**Implementation:**

```c
// SPAWN bytecode instruction
void execute_SPAWN(VM* vm) {
    Value fn_val = pop(vm);

    // Create new task
    Task* task = task_create(
        (Function*)untag_pointer(fn_val),
        vm->current_ns,
        vm->current_task  // Parent task
    );

    // Add to scheduler
    scheduler_add_task(vm->scheduler, task);

    // Push task handle
    push(vm, make_task_handle(task));
}

Task* task_create(Function* fn, Namespace* ns, Task* parent) {
    Task* task = malloc(sizeof(Task));
    task->id = next_task_id();
    task->state = TASK_READY;
    task->priority = parent ? parent->priority : 128;  // Inherit priority

    // Allocate stack
    task->stack_size = 4096;  // Initial stack size
    task->stack = malloc(sizeof(Value) * task->stack_size);
    task->stack_pointer = 0;
    task->frame_pointer = 0;
    task->pc = fn->code_idx;

    // Setup initial frame for function call
    push_task(task, make_handler_value(NULL));
    push_task(task, tag_pointer(fn));
    push_task(task, make_int(0));  // No previous frame
    push_task(task, make_int(0));  // No return address
    task->frame_pointer = 0;

    // Thread-local storage (inherit from parent)
    task->thread_locals = parent ? copy_hashmap(parent->thread_locals)
                                 : hashmap_new();

    task->parent = parent;
    task->children = vector_new();
    if (parent) {
        vector_push(parent->children, task);
    }

    return task;
}
```

#### Scheduling Algorithm

**Round-robin with priorities:**

1. OS threads continuously execute tasks from ready queues
2. Higher priority tasks are scheduled first
3. Within same priority, round-robin
4. Time slice based on instruction count
5. Optional work stealing for load balancing

**Worker thread loop:**

```c
void* worker_thread(void* arg) {
    Scheduler* sched = (Scheduler*)arg;

    while (!sched->shutdown) {
        // Get next task to run
        Task* task = scheduler_get_next_task(sched);

        if (task == NULL) {
            // No work available, wait for signal
            pthread_mutex_lock(&sched->lock);
            pthread_cond_wait(&sched->work_available, &sched->lock);
            pthread_mutex_unlock(&sched->lock);
            continue;
        }

        // Execute task for one time slice
        task_run_time_slice(task, sched->time_slice_instructions);

        // Handle task state after execution
        switch (task->state) {
            case TASK_YIELDED:
            case TASK_READY:
                // Re-enqueue task
                scheduler_enqueue_task(sched, task);
                break;

            case TASK_BLOCKED:
                // Move to blocked queue
                scheduler_block_task(sched, task);
                break;

            case TASK_COMPLETED:
            case TASK_FAILED:
                // Cleanup and notify waiters
                scheduler_complete_task(sched, task);
                break;

            case TASK_RUNNING:
                // Should not happen
                assert(false);
                break;
        }

        sched->context_switches++;
    }

    return NULL;
}

Task* scheduler_get_next_task(Scheduler* sched) {
    pthread_mutex_lock(&sched->lock);

    // Check priority levels from high to low
    for (int pri = 255; pri >= 0; pri--) {
        if (!queue_empty(sched->ready_queues[pri])) {
            Task* task = queue_dequeue(sched->ready_queues[pri]);
            task->state = TASK_RUNNING;
            pthread_mutex_unlock(&sched->lock);
            return task;
        }
    }

    pthread_mutex_unlock(&sched->lock);
    return NULL;  // No tasks available
}
```

#### Task Execution (Time Slice)

```c
void task_run_time_slice(Task* task, uint32_t max_instructions) {
    uint32_t instructions_executed = 0;

    while (instructions_executed < max_instructions) {
        // Fetch and execute instruction
        uint8_t opcode = bytecode[task->pc];
        task->pc++;

        switch (opcode) {
            case OP_YIELD:
                // Explicit yield
                task->state = TASK_YIELDED;
                return;

            case OP_CALL_NATIVE:
                // Native FFI call - may block
                execute_call_native(task);
                if (task->state == TASK_BLOCKED) {
                    return;  // Task blocked on FFI
                }
                break;

            case OP_RETURN:
                execute_return(task);
                if (task->frame_pointer == 0) {
                    // Returned from top-level function
                    task->state = TASK_COMPLETED;
                    return;
                }
                break;

            // ... other instructions ...

            default:
                execute_instruction(task, opcode);
                break;
        }

        instructions_executed++;
        task->total_instructions++;

        // Check for unhandled exception
        if (task->state == TASK_FAILED) {
            return;
        }
    }

    // Time slice exhausted, yield
    task->state = TASK_YIELDED;
    task->time_slice += instructions_executed;
}
```

#### Automatic Yield Points

The compiler inserts automatic yield points to ensure responsiveness:

```c
void compile_loop(Compiler* c, Loop* loop) {
    emit_label(c, loop_start);

    // Compile loop condition and body
    compile_expr(c, loop->condition, false);
    emit_jump_if_false(c, loop_end);
    compile_expr(c, loop->body, false);

    // Automatic yield point at loop back-edge
    emit(c, OP_YIELD);

    emit_jump(c, loop_start);
    emit_label(c, loop_end);
}
```

**Benefits:**
- Long-running loops automatically yield
- No task can monopolize CPU
- Can be disabled in performance-critical code with annotations

#### FFI and Blocking Operations

When a task calls a native function that may block (I/O, system calls), the task is moved to a separate thread:

```c
void execute_call_native(Task* task) {
    Value fn_val = pop(task);
    NativeFunction* native = (NativeFunction*)untag_pointer(fn_val);

    if (native->blocking) {
        // Blocking call - dispatch to thread pool
        task->state = TASK_BLOCKED;
        task->blocked_on = native;

        // Submit to FFI thread pool
        ffi_thread_pool_submit(task, native);
    } else {
        // Non-blocking native call - execute inline
        Value result = native->fn_ptr(task, n_args);
        push(task, result);
    }
}

// FFI thread pool
void ffi_thread_pool_submit(Task* task, NativeFunction* fn) {
    FFIJob* job = malloc(sizeof(FFIJob));
    job->task = task;
    job->function = fn;

    // Submit to separate thread pool for blocking operations
    pthread_mutex_lock(&ffi_pool.lock);
    queue_enqueue(ffi_pool.job_queue, job);
    pthread_cond_signal(&ffi_pool.work_available);
    pthread_mutex_unlock(&ffi_pool.lock);
}

void* ffi_worker_thread(void* arg) {
    while (!shutdown) {
        FFIJob* job = ffi_pool_get_job();
        if (job == NULL) continue;

        // Execute blocking native function
        Value result = job->function->fn_ptr(job->task, n_args);

        // Store result and mark task as ready
        push(job->task, result);
        job->task->state = TASK_READY;
        job->task->blocked_on = NULL;

        // Re-enqueue task in scheduler
        scheduler_enqueue_task(scheduler, job->task);

        free(job);
    }
    return NULL;
}
```

#### Task Communication

**Channels for message passing:**

```clojure
;; Create channel
(def ch (chan 10))  ; Buffered channel with size 10

;; Task operations (yield if channel not ready)
(>! ch value)       ; Send to channel (yields task if full)
(let [val (<! ch)]  ; Receive from channel (yields task if empty)
  (println "Received:" val))

;; Blocking operations (block OS thread - use sparingly!)
(>!! ch value)      ; Blocking send (blocks OS thread if full)
(<!! ch)            ; Blocking receive (blocks OS thread if empty)
```

**Semantics (following Clojure core.async):**

- `<!` and `>!` - Cooperative operations inside tasks
  - May yield the current task if channel is not ready
  - Task is resumed when channel becomes ready
  - Preferred for use inside `spawn` tasks

- `<!!` and `>!!` - Thread-blocking operations
  - Block the OS thread (not recommended inside tasks!)
  - Useful for main thread or FFI context
  - Should be rare - tasks should use `<!` and `>!`

**Implementation:**

```c
struct Channel {
    Object     header;
    Queue*     buffer;        // Message buffer
    int        capacity;      // Buffer size (0 = unbuffered)
    Queue*     waiting_send;  // Tasks waiting to send
    Queue*     waiting_recv;  // Tasks waiting to receive
    pthread_mutex_t lock;
    bool       closed;
};

// >! - Cooperative send (yields task if channel full)
void channel_send_yielding(Task* task, Channel* ch, Value val) {
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        throw_error(task, "Channel closed");
        return;
    }

    if (queue_size(ch->buffer) < ch->capacity) {
        // Buffer has space, enqueue immediately
        queue_enqueue(ch->buffer, val);

        // Wake up waiting receiver
        if (!queue_empty(ch->waiting_recv)) {
            Task* receiver = queue_dequeue(ch->waiting_recv);
            receiver->state = TASK_READY;
            scheduler_enqueue_task(scheduler, receiver);
        }

        pthread_mutex_unlock(&ch->lock);
    } else {
        // Buffer full, yield task (cooperative)
        task->state = TASK_BLOCKED;
        task->blocked_on = ch;
        queue_enqueue(ch->waiting_send, task);
        pthread_mutex_unlock(&ch->lock);
        // Task will be resumed when channel has space
    }
}

// >!! - Thread-blocking send (blocks OS thread if channel full)
bool channel_send_blocking(Channel* ch, Value val) {
    pthread_mutex_lock(&ch->lock);

    if (ch->closed) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }

    // Wait until buffer has space (blocks OS thread!)
    while (queue_size(ch->buffer) >= ch->capacity) {
        pthread_cond_wait(&ch->space_available, &ch->lock);
    }

    queue_enqueue(ch->buffer, val);

    // Wake up waiting receiver
    pthread_cond_signal(&ch->data_available);

    pthread_mutex_unlock(&ch->lock);
    return true;
}

// Similar implementations for <! and <!! (receive operations)
```

#### Work Stealing

Optional work-stealing for better load balancing:

```c
Task* scheduler_get_next_task_with_stealing(Scheduler* sched, int thread_id) {
    // Try local queue first
    Task* task = scheduler_get_next_task(sched);
    if (task != NULL) return task;

    // No local work, try stealing from other threads
    if (sched->work_stealing_enabled) {
        for (int i = 0; i < sched->n_threads; i++) {
            if (i == thread_id) continue;

            // Try to steal from thread i
            task = queue_steal(sched->ready_queues[i]);
            if (task != NULL) {
                sched->steals++;
                return task;
            }
        }
    }

    return NULL;
}
```

#### Task Lifecycle

```
CREATE → READY → RUNNING → YIELDED → RUNNING → ... → COMPLETED
                    ↓
                 BLOCKED → READY → RUNNING
                    ↓
                 FAILED
```

#### Performance Characteristics

**Lightweight tasks:**
- ~4KB initial stack per task
- Minimal per-task overhead (~200 bytes + stack)
- Can create thousands/millions of tasks
- Context switch is just saving/restoring VM state

**OS thread pool:**
- Default: Number of CPU cores
- Tasks multiplexed onto threads
- M:N threading model (M tasks on N threads)

**Scheduling overhead:**
- Lock-free queues where possible
- Per-priority queues reduce contention
- Work stealing improves load balancing
