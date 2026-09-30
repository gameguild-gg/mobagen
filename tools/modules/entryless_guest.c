/* Entry-less wasm fixture: exports a plain function but no annotation table. */

int entryless_guest_value(int input) { return input + 1; }
