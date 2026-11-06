/* BEER_PLATFORM=embedder: the runtime on an embedder's reactor.h, the way
 * beeros parks tasks on IRQs. Built by `make test-embedder` (its own
 * object set, see tests/embedder/reactor/Makefile), not by the tests/X/
 * rule: it replaces src/io/reactor.c.
 *
 * The fake reactor behaves like beeros's: an "interrupt handler" sets a
 * pending bit per IRQ, IRQ n is fd IRQ_FD0 + n, reactor_poll reports the
 * watched fds whose bit is set without clearing it, and a wait (wfi) runs
 * the next scripted interrupt and returns. */

#include "test.h"
#include "beer.h"
#include "reactor.h"
#include "scheduler.h"
#include "vm.h"
#include <stdlib.h>

#define IRQ_FD0 32
#define N_IRQS  8

struct Reactor { int dummy; };
static struct Reactor the_reactor;

static volatile unsigned pending;   /* set by the "interrupt handler" */
static bool watched[N_IRQS];

/* Interrupts that arrive during the next waits, in order: an IRQ number,
 * or CTRL_C for vm_request_interrupt (the UART handler seeing ^C) */
#define CTRL_C (-1)
static int script[8];
static int n_script, next_script;

static int waits;        /* reactor_poll calls with timeout != 0 */
static int native_calls;

static void reset(const int* irqs, int n) {
    pending = 0;
    for (int i = 0; i < N_IRQS; i++) watched[i] = false;
    for (int i = 0; i < n; i++) script[i] = irqs[i];
    n_script = n;
    next_script = 0;
    waits = 0;
    native_calls = 0;
}

static void interrupt_handler(int irq) {
    if (irq == CTRL_C) vm_request_interrupt();
    else pending |= 1u << irq;
}

Reactor* reactor_new(void) { return &the_reactor; }
void reactor_free(Reactor* r) { (void)r; }

int reactor_set(Reactor* r, int fd, bool read, bool write) {
    (void)r; (void)write;
    int irq = fd - IRQ_FD0;
    if (irq < 0 || irq >= N_IRQS) return -1;
    watched[irq] = read;
    return 0;
}

int reactor_poll(Reactor* r, ReactorEvent* out, int max_events, int timeout_ms) {
    (void)r;
    int n = 0;
    for (int irq = 0; irq < N_IRQS && n < max_events; irq++) {
        if (watched[irq] && (pending & (1u << irq))) {
            out[n++] = (ReactorEvent){ IRQ_FD0 + irq, true, false };
        }
    }
    if (n > 0 || timeout_ms == 0) return n;
    /* wfi: the next interrupt runs its handler, then the wait returns */
    waits++;
    if (next_script < n_script) interrupt_handler(script[next_script++]);
    else if (waits > 100) abort();  /* nothing would ever wake the test */
    return 0;
}

/* (irq-wait! n): consume IRQ n's pending bit, or park until it is set.
 * Woken, the native runs again from the start and consumes it then. */
static BeerValue native_irq_wait(BeerVM* vm, int argc, BeerValue* argv) {
    native_calls++;
    if (argc != 1 || !beer_is_int(argv[0])) {
        beer_vm_error(vm, "irq-wait!: expects an IRQ number");
        return beer_nil();
    }
    int irq = (int)beer_to_int(argv[0]);
    if (pending & (1u << irq)) {
        pending &= ~(1u << irq);
        return beer_int(irq);
    }
    if (!scheduler_can_park(vm)) {
        beer_vm_error(vm, "irq-wait!: not in a task");
        return beer_nil();
    }
    scheduler_park_native(vm, IRQ_FD0 + irq, false);
    return beer_nil();
}

static BeerState* B;

/* Two tasks wait on different IRQs, which fire in the other order: each
 * wakes on its own, no nesting, and the CPU waits once per interrupt */
TEST(two_waiters) {
    int irqs[] = { 2, 1 };
    reset(irqs, 2);
    BeerValue v = beer_eval_expr(B,
        "(let [log (atom [])"
        "      a (spawn (fn [] (swap! log conj (irq/wait! 1))))"
        "      b (spawn (fn [] (swap! log conj (irq/wait! 2))))]"
        "  (await a) (await b) @log)");
    ASSERT(beer_error(B) == NULL, "two waiters run without error");
    ASSERT_EQ(beer_length(v), 2, "both tasks logged");
    ASSERT_EQ(beer_to_int(beer_nth(v, 0)), 2, "IRQ 2's waiter woke first");
    ASSERT_EQ(beer_to_int(beer_nth(v, 1)), 1, "IRQ 1's waiter woke second");
    ASSERT_EQ(waits, 2, "one idle wait per interrupt, no spinning");
    ASSERT_EQ(native_calls, 4, "each native parked once and ran again once");
    ASSERT_EQ(pending, 0, "the waiters consumed their bits");
    beer_value_release(v);
    return NULL;
}

/* A bit set before the task looks is consumed without parking */
TEST(latched_before_wait) {
    reset(NULL, 0);
    pending = 1u << 3;
    BeerValue v = beer_eval_expr(B, "(await (spawn (fn [] (irq/wait! 3))))");
    ASSERT(beer_error(B) == NULL, "latched IRQ runs without error");
    ASSERT_EQ(beer_to_int(v), 3, "irq-wait! returns the IRQ");
    ASSERT_EQ(waits, 0, "no idle wait");
    ASSERT_EQ(native_calls, 1, "no park");
    return NULL;
}

/* An interrupt for nobody's IRQ: the wait returns, the scheduler finds
 * nothing to run, and waits again until the right one comes */
TEST(unrelated_interrupt) {
    int irqs[] = { 5, 4 };
    reset(irqs, 2);
    BeerValue v = beer_eval_expr(B, "(await (spawn (fn [] (irq/wait! 4))))");
    ASSERT(beer_error(B) == NULL, "runs without error");
    ASSERT_EQ(beer_to_int(v), 4, "woken by its own IRQ");
    ASSERT_EQ(waits, 2, "waited through the unrelated interrupt");
    ASSERT_EQ(pending, 1u << 5, "the unrelated bit stays latched");
    return NULL;
}

/* ^C while idle: the wait returns after the interrupt, so the scheduler
 * sees the request and interrupts the foreground form */
TEST(ctrl_c_while_idle) {
    int irqs[] = { CTRL_C };
    reset(irqs, 1);
    int rc = beer_do_string(B, "(await (spawn (fn [] (irq/wait! 6))))");
    ASSERT(rc != 0, "the waiting form is interrupted");
    ASSERT(strstr(beer_error(B), "nterrupt") != NULL, "by the interrupt");
    ASSERT_EQ(waits, 1, "one idle wait");
    beer_clear_error(B);
    return NULL;
}

static const char* all_tests(void) {
    RUN_TEST(two_waiters);
    RUN_TEST(latched_before_wait);
    RUN_TEST(unrelated_interrupt);
    RUN_TEST(ctrl_c_while_idle);
    return NULL;
}

int main(void) {
    B = beer_open();
    beer_register(B, "irq", "wait!", native_irq_wait);
    RUN_SUITE(all_tests);
    beer_close(B);
    return 0;
}
