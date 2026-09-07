# doctest - test framework shared by test/CoreTests and the apps' formal test
# runners (flocking-tests, life-tests). Added once here; consumers just link
# doctest::doctest. Unconditional because test/ also configures on Emscripten;
# the console runner targets themselves are desktop-only (NOT EMSCRIPTEN).
CPMAddPackage("gh:onqtam/doctest@2.4.12")
