#include "include/k3libc.h"
int main() { int t = k3_timer_ms(); k3_printf("Uptime: %d ms\n", t, 0,0,0); return 0; }
