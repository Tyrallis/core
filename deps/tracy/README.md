# Tracy client

The client half of the [Tracy Profiler](https://github.com/wolfpld/tracy), release **v0.14.1**
(commit 30997d5c), BSD-3-Clause (see `LICENSE`). `public/` is the release's `public/` folder as
it ships, without the Fortran binding.

WarcraftXL.dll compiles `public/TracyClient.cpp` once, with the options set in core's
`CMakeLists.txt` (`WXL_PROFILE`): on-demand, started by the runtime rather than at load, no crash
handler, no system tracing, no call stacks. The viewer that reads it is Tracy **0.14.x**: the
protocol changes between releases, and a viewer of another version refuses the connection.

To move to another release, replace `public/` and `LICENSE` with that release's and update the
version here, in core's `CMakeLists.txt` and in the SDK's profiling guide.
