-- Install examples/playtest-platformer.lua as main.lua before execution.
-- This script advances 60 frames in one tool execution.
tools.start_test("main.lua", 17)
local initial = tools.inspect_runtime()
assert(initial.running and initial.state, "Platformer did not start")
assert(initial.state.player.grounded, "Player must start on the roof")

tools.step_frames(12, tools.RIGHT, 0, 0)
local moved = tools.inspect_runtime()
assert(moved.running and moved.state, "Runtime stopped during movement")
assert(moved.state.player.x > initial.state.player.x, "RIGHT must move the player")
assert(moved.state.score == 1, "Walking must collect the first light")

tools.step_frames(8, tools.RIGHT | tools.A, 0, 0)
local jumped = tools.inspect_runtime()
assert(jumped.running and jumped.state, "Runtime stopped during the jump")
assert(jumped.state.player.y < moved.state.player.y, "A must raise the player")
assert(not jumped.state.player.grounded, "Jump must leave the roof")

tools.step_frames(24, tools.RIGHT, 0, 0)
tools.step_frames(16, 0, 0, 0)
local landed = tools.inspect_runtime()
assert(landed.running and landed.state, "Runtime stopped before landing")
assert(landed.state.player.grounded, "Player must land on the raised roof")
assert(landed.state.player.y == 114, "Player must land on the first platform")
assert(landed.state.score == 2, "Jump must collect the second light")
assert(landed.state.frame == 60, "Test must advance exactly 60 frames")
assert(landed.state.scene == "rooftops", "Test must remain in the rooftop scene")

return {
  demo = "Lantern Run",
  frames = landed.state.frame,
  right = true,
  jump = true,
  landing = true,
  score = landed.state.score,
}
