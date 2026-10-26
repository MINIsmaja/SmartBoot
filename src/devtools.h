#pragma once

namespace devtools {
void begin();  // remembers an unexpected reset (crash, watchdog, brownout) for `info`
void poll();   // handles serial dev commands (screenshots, injected keys)
}
