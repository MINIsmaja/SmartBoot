#include <vector>
#include "screens.h"

namespace {
std::vector<Screen*> stack;
}

namespace nav {

void push(Screen* s) {
    stack.push_back(s);
    s->onEnter();
}

void pop() {
    if (stack.size() > 1) stack.pop_back();
}

void reset(Screen* s) {
    stack.clear();
    push(s);
}

Screen* top() { return stack.back(); }

void render() {
    size_t base = stack.size() - 1;
    while (base > 0 && stack[base]->isOverlay()) base--;
    for (size_t i = base; i < stack.size(); i++) {
        if (i > base) ui::darken();
        stack[i]->draw();
    }
    ui::drawToast();
    canvas.pushSprite(&M5Cardputer.Display, 0, 0);
}

}  // namespace nav
