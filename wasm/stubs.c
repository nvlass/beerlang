/* WASM stubs: tcp, udp, shell, crypto, aot are not available in the browser.
 * Empty registration functions satisfy the calls in namespace.c. */

void core_register_tcp(void)    {}
void core_register_udp(void)    {}
void core_register_shell(void)  {}
void core_register_crypto(void) {}
void core_register_aot(void)    {}
